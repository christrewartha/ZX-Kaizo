#include "gcc8_c_support.h"
#include <hardware/dmabits.h>
#include "sound.h"
#include "song.h"
#include "sound_data.h"
#include "hw.h"

#define MUSIC_CHANNELS 3
#define SFX_CHANNEL    3

#define WAVE_TRI   0x10
#define WAVE_SAW   0x20
#define WAVE_PULSE 0x40
#define WAVE_NOISE 0x80

// Pulse width sweeps bounce between these (12 bits, as on the SID)
#define PW_MIN     0x0200
#define PW_MAX     0x0e00

// Vibrato starts once a note has rung for this many frames
#define VIBRATO_DELAY 12

#define FULL       (64 << 8)    // envelope, 8.8 fixed point

// How loud each music channel is: bass and drums, lead, arpeggios
static const UBYTE channel_level[MUSIC_CHANNELS] = { 64, 44, 30 };

#define ATTACK  0
#define DECAY   1

typedef struct {
    const unsigned char *pos;   // next (note, instrument, frames)
    UBYTE section;
    UBYTE left;                 // frames until it is read
    UBYTE note;                 // 1-84, or 0 for silence
    const instrument_t *inst;
    UBYTE gate;                 // key held
    UBYTE stage;                // ATTACK or DECAY (then sustain)
    UBYTE age;                  // frames since the note started
    UBYTE arp_pos;
    UBYTE drum;                 // playing a one-shot drum sample
    UWORD env;                  // volume, 0..FULL
    UWORD pw;
    BYTE pwm;
    const BYTE *sample;         // the loop Paula has been given
} voice_t;

static voice_t voices[MUSIC_CHANNELS];
static volatile UBYTE ready;
static volatile UBYTE restart_request;

// The SID's envelope times in milliseconds, as volume steps a frame
static const UWORD attack_ms[16] = { 2, 8, 16, 24, 38, 56, 68, 80, 100, 250, 500, 800, 1000, 3000, 5000, 8000 };
static UWORD attack_step[16], decay_step[16];

// ---------------------------------------------------------------- Paula

static void wait_lines(UWORD n) {
    UWORD line = custom->vhposr >> 8;

    while (n) {
        UWORD now = custom->vhposr >> 8;
        if (now != line) {
            line = now;
            n--;
        }
    }
}

// Start a channel afresh on a sample: Paula only picks up a new pointer at
// the end of the current loop, unless the channel is stopped and started
static void retrigger(int ch, const BYTE *ptr, UWORD words, UWORD period) {
    custom->dmacon = 1 << ch;
    wait_lines(4);              // long enough for Paula to notice
    custom->aud[ch].ac_ptr = (UWORD *)ptr;
    custom->aud[ch].ac_len = words;
    custom->aud[ch].ac_per = period;
    custom->dmacon = DMAF_SETCLR | (1 << ch);
}

// What to loop next (used at the end of the current loop)
static void set_loop(int ch, const BYTE *ptr, UWORD words) {
    custom->aud[ch].ac_ptr = (UWORD *)ptr;
    custom->aud[ch].ac_len = words;
}

static const BYTE *sample(UWORD offset) {
    return (const BYTE *)sound_samples + offset;
}

// --------------------------------------------------------------- music

static void song_start(void) {
    for (int v = 0; v < MUSIC_CHANNELS; v++) {
        voice_t *m = &voices[v];
        m->section = 0;
        m->pos = song_streams[0][v];
        m->left = 1;
        m->note = 0;
        m->gate = 0;
        m->env = 0;
        m->drum = 0;
        m->inst = song_instruments;
        m->sample = sample(S_SILENCE);
        set_loop(v, m->sample, 1);
        custom->aud[v].ac_vol = 0;
    }
}

static const unsigned char *next_pos(voice_t *m, int v) {
    const unsigned char *p = m->pos;

    if (*p == SONG_END) {
        UBYTE s = m->section + 1;
        if (s >= song_sections) s = song_loop;
        m->section = s;
        p = m->pos = song_streams[s][v];
    }
    return p;
}

static void start_note(voice_t *m, int v, UBYTE note, UBYTE inst) {
    const instrument_t *in = &song_instruments[inst];

    m->inst = in;
    m->note = note;
    m->age = 0;
    m->arp_pos = 0;
    m->pw = in->pw;
    m->pwm = in->pwm;
    m->gate = 1;
    m->stage = ATTACK;
    m->env = 0;

    if (in->fixed) {
        // Drums: one of the sampled kits, chosen by its sound on the SID
        const BYTE *p;
        UWORD words;
        if (in->wave & WAVE_NOISE) {
            if (in->gate <= 1) { p = sample(S_HAT); words = S_HAT_WORDS; }
            else { p = sample(S_SNARE); words = S_SNARE_WORDS; }
        } else {
            p = sample(S_KICK); words = S_KICK_WORDS;
        }
        m->drum = 1;
        custom->aud[v].ac_vol = channel_level[v];
        retrigger(v, p, words, DRUM_PERIOD);
        m->sample = 0;
    } else if (m->drum) {
        // Back from a drum: cut it off, so the note starts now
        m->drum = 0;
        m->sample = 0;
        custom->aud[v].ac_vol = 0;
        retrigger(v, sample(S_SILENCE), 1, 124);
    }
}

static void next_event(voice_t *m, int v) {
    const unsigned char *p = next_pos(m, v);

    m->left = p[2];
    m->pos = p + 3;
    if (p[0] == SONG_HOLD) return;
    if (!p[0]) {
        m->gate = 0;
        return;
    }
    start_note(m, v, p[0], p[1]);
}

static void voice_tick(int v) {
    voice_t *m = &voices[v];
    const instrument_t *in = m->inst;
    UBYTE note, len, type;
    UWORD period, target;
    const BYTE *s;

    if (!--m->left) {
        next_event(m, v);
        in = m->inst;
    } else if (m->left == 1 && next_pos(m, v)[0] != SONG_HOLD) {
        m->gate = 0;            // let go a frame early, as on the SID
    }
    if (m->age < 255) m->age++;

    if (m->drum) {
        // The one-shot plays out, then silence
        if (m->age == 2) set_loop(v, sample(S_SILENCE), 1);
        return;
    }
    if (!m->note) {
        custom->aud[v].ac_vol = 0;
        return;
    }
    if (in->gate && m->age == in->gate) m->gate = 0;

    // The envelope
    if (m->gate) {
        if (m->stage == ATTACK) {
            UWORD step = attack_step[in->ad >> 4];
            m->env = (m->env + step >= FULL) ? FULL : m->env + step;
            if (m->env == FULL) m->stage = DECAY;
        } else {
            target = (in->sr >> 4) * (FULL / 15);
            if (m->env > target) {
                UWORD step = decay_step[in->ad & 15];
                m->env = (m->env - target > step) ? m->env - step : target;
            }
        }
    } else {
        UWORD step = decay_step[in->sr & 15];
        m->env = m->env > step ? m->env - step : 0;
    }

    // The pitch: the note, its chord, its vibrato
    note = m->note;
    if (in->arp_n) {
        note += in->arp[m->arp_pos];
        if (++m->arp_pos >= in->arp_n) m->arp_pos = 0;
        if (note > 84) note = 84;
    }
    len = note_length[note - 1];
    period = note_period[note - 1];
    if (in->vibrato && m->age > VIBRATO_DELAY) {
        UBYTE phase = m->age & 7;
        if (phase & 4) phase = 8 - phase;
        period += (period >> 9) * phase;
        period -= period >> 8;
    }

    // The waveform: pulses come in four widths, which the sweep moves
    // between
    if (m->pwm) {
        m->pw += m->pwm;
        if (m->pw >= PW_MAX) m->pwm = -(m->pwm > 0 ? m->pwm : -m->pwm);
        else if (m->pw <= PW_MIN) m->pwm = m->pwm > 0 ? m->pwm : -m->pwm;
    }
    if (in->wave & WAVE_NOISE) {
        s = sample(S_NOISE);
        if (s != m->sample) set_loop(v, s, S_NOISE_WORDS);
    } else {
        if (in->wave & WAVE_PULSE) {
            UBYTE d = m->pw >> 9;                   // eighths, 0-7
            if (d > 4) d = 8 - d;
            if (d < 1) d = 1;
            type = 1 + d;
        } else {
            type = (in->wave & WAVE_SAW) ? 1 : 0;
        }
        s = sample(wave_offset[type][len]);
        if (s != m->sample) set_loop(v, s, wave_words[len]);
    }
    m->sample = s;
    custom->aud[v].ac_per = period;
    custom->aud[v].ac_vol = (m->env >> 8) * channel_level[v] >> 6;
}

// --------------------------------------------------------------- effects

// As on the C64: { attack/decay, sustain/release, then (SID control,
// pitch high byte) a frame each, then 0 }. Here they get a channel of
// their own.
static const unsigned char sfx_jump[] = {
    0x00, 0xA4,
    0x41, 0x0E, 0x41, 0x11, 0x41, 0x14, 0x41, 0x17, 0x41, 0x1A, 0x41, 0x1D, 0x40, 0x1D, 0
};
static const unsigned char sfx_wall_jump[] = {
    0x00, 0xA4,
    0x41, 0x16, 0x41, 0x1A, 0x41, 0x1E, 0x41, 0x22, 0x41, 0x26, 0x41, 0x2A, 0x40, 0x2A, 0
};
static const unsigned char sfx_death[] = {
    0x00, 0xF8,
    0x81, 0x40, 0x81, 0x30, 0x81, 0x28, 0x81, 0x20, 0x81, 0x1A, 0x81, 0x16, 0x81, 0x12,
    0x81, 0x10, 0x81, 0x0E, 0x81, 0x0C, 0x81, 0x0A, 0x81, 0x09, 0x81, 0x08, 0x81, 0x07,
    0x81, 0x06, 0x81, 0x05, 0x81, 0x04, 0x80, 0x04, 0
};
static const unsigned char sfx_trophy[] = {
    0x00, 0xC6,
    0x41, 0x45, 0x41, 0x45, 0x41, 0x45, 0x41, 0x57, 0x41, 0x57, 0x41, 0x57,
    0x41, 0x68, 0x41, 0x68, 0x41, 0x68, 0x41, 0x8B, 0x41, 0x8B, 0x41, 0x8B,
    0x41, 0x8B, 0x41, 0x8B, 0x41, 0x8B, 0x40, 0x8B, 0
};
static const unsigned char sfx_clear[] = {
    0x00, 0xC8,
    0x41, 0x22, 0x41, 0x22, 0x41, 0x22, 0x41, 0x22, 0x41, 0x2B, 0x41, 0x2B, 0x41, 0x2B, 0x41, 0x2B,
    0x41, 0x34, 0x41, 0x34, 0x41, 0x34, 0x41, 0x34, 0x41, 0x45, 0x41, 0x45, 0x41, 0x45, 0x41, 0x45,
    0x41, 0x57, 0x41, 0x57, 0x41, 0x57, 0x41, 0x57, 0x41, 0x68, 0x41, 0x68, 0x41, 0x68, 0x41, 0x68,
    0x41, 0x8B, 0x41, 0x8B, 0x41, 0x8B, 0x41, 0x8B, 0x41, 0x8B, 0x41, 0x8B, 0x41, 0x8B, 0x41, 0x8B,
    0x40, 0x8B, 0
};
static const unsigned char sfx_thunder[] = {
    0x08, 0xFB,
    0x81, 0x06, 0x81, 0x06, 0x81, 0x05, 0x81, 0x05, 0x81, 0x04, 0x81, 0x04, 0x81, 0x04,
    0x81, 0x03, 0x81, 0x03, 0x81, 0x03, 0x81, 0x03, 0x81, 0x02, 0x80, 0x02, 0
};
static const unsigned char sfx_start[] = {
    0x00, 0xC6,
    0x41, 0x11, 0x41, 0x11, 0x41, 0x16, 0x41, 0x16, 0x41, 0x1A, 0x41, 0x1A, 0x41, 0x22, 0x41, 0x22,
    0x41, 0x2B, 0x41, 0x2B, 0x41, 0x34, 0x41, 0x34, 0x41, 0x45, 0x41, 0x45, 0x41, 0x45, 0x40, 0x45, 0
};
static const unsigned char sfx_land[] = {
    0x00, 0x62,
    0x81, 0x0C, 0x81, 0x08, 0x80, 0x08, 0
};

static const unsigned char *const sfx_table[] = {
    sfx_jump, sfx_wall_jump, sfx_death, sfx_trophy, sfx_clear, sfx_thunder, sfx_start, sfx_land
};

#define SFX_LEVEL 56
#define NO_SFX    0xff
static volatile UBYTE sfx_request = NO_SFX;
static const unsigned char *sfx_pos;
static UBYTE sfx_vol, sfx_noise;
static const BYTE *sfx_sample;

static void sfx_tick(void) {
    UBYTE n = sfx_request;
    const BYTE *s;

    if (n != NO_SFX) {
        sfx_request = NO_SFX;
        sfx_pos = sfx_table[n] + 2;
        sfx_sample = 0;
    }
    if (!sfx_pos) {
        // Let the last note die away
        if (sfx_vol) sfx_vol = sfx_vol > 6 ? sfx_vol - 6 : 0;
        custom->aud[SFX_CHANNEL].ac_vol = sfx_vol;
        return;
    }
    if (!sfx_pos[0]) {
        sfx_pos = 0;
        return;
    }

    UBYTE ctrl = sfx_pos[0], pitch = sfx_pos[1];
    sfx_pos += 2;
    sfx_noise = (ctrl & WAVE_NOISE) != 0;
    if (sfx_noise) {
        s = sample(S_NOISE);
        if (s != sfx_sample) set_loop(SFX_CHANNEL, s, S_NOISE_WORDS);
        custom->aud[SFX_CHANNEL].ac_per = 124 + (UWORD)(255 - pitch) * 2;
    } else {
        UBYTE len = sfx_length[pitch];
        s = sample(wave_offset[(ctrl & WAVE_TRI) ? 0 : 5][len]);
        if (s != sfx_sample) set_loop(SFX_CHANNEL, s, wave_words[len]);
        custom->aud[SFX_CHANNEL].ac_per = sfx_period[pitch];
    }
    sfx_sample = s;
    if (ctrl & 1) sfx_vol = SFX_LEVEL;
    else sfx_vol = sfx_vol > 12 ? sfx_vol - 12 : 0;
    custom->aud[SFX_CHANNEL].ac_vol = sfx_vol;
}

// ---------------------------------------------------------------- outside

void sound_init(void) {
    for (int i = 0; i < 16; i++) {
        UWORD frames = attack_ms[i] / 20;
        attack_step[i] = FULL / (frames ? frames : 1);
        frames = attack_ms[i] * 3 / 20;
        decay_step[i] = FULL / (frames ? frames : 1);
    }
    custom->dmacon = DMAF_AUDIO;
    custom->adkcon = 0x00ff;    // no channel modulates another
    for (int ch = 0; ch < 4; ch++) {
        custom->aud[ch].ac_ptr = (UWORD *)sample(S_SILENCE);
        custom->aud[ch].ac_len = 1;
        custom->aud[ch].ac_per = 124;
        custom->aud[ch].ac_vol = 0;
    }
    song_start();
    custom->dmacon = DMAF_SETCLR | DMAF_AUDIO;
    ready = 1;
}

void sound_play(unsigned char sfx) {
    sfx_request = sfx;
}

void sound_restart(void) {
    restart_request = 1;
}

void sound_tick(void) {
    if (!ready) return;
    if (restart_request) {
        restart_request = 0;
        song_start();
    }
    for (int v = 0; v < MUSIC_CHANNELS; v++) voice_tick(v);
    sfx_tick();
}

void sound_stop(void) {
    ready = 0;
    custom->dmacon = DMAF_AUDIO;
    for (int ch = 0; ch < 4; ch++) custom->aud[ch].ac_vol = 0;
}
