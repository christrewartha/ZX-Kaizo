#include "sound.h"
#include "song.h"

// SID pitches for notes 1-84 (build/c64gen/tables.c)
extern const unsigned int sid_freq[84];

#define SID           ((volatile unsigned char *)0xD400)
#define V_FREQ_LO     0
#define V_FREQ_HI     1
#define V_PW_LO       2
#define V_PW_HI       3
#define V_CTRL        4
#define V_AD          5
#define V_SR          6
#define SID_FC_HI     0x16
#define SID_RES_FILT  0x17
#define SID_MODE_VOL  0x18

#define GATE          0x01
#define RESONANCE     0xC0      // strong, for the filtered bass

#define SFX_VOICE     2         // voice C gives way to sound effects
#define SFX_REG       (SFX_VOICE * 7)

// Pulse width sweeps bounce between these
#define PW_MIN        0x0200
#define PW_MAX        0x0E00

// The filter cutoff (high byte) drifts between these
#define FC_MIN        0x14
#define FC_MAX        0x58

// Vibrato starts once a note has rung for this many frames
#define VIBRATO_DELAY 12

// Slid pitches (drums) stop falling here
#define SLIDE_FLOOR   0x0100

typedef struct {
    const unsigned char *pos;   // next (note, instrument, frames)
    unsigned char section;
    unsigned char left;         // frames until it is read
    unsigned char note;         // 1-84, or 0 for silence
    const instrument_t *inst;
    unsigned char ctrl;         // waveform and gate
    unsigned char age;          // frames since the note started
    unsigned char arp_pos;
    unsigned int pw;
    signed char pwm;            // the sweep's current direction and speed
    unsigned int freq;          // for fixed and sliding pitches
} voice_t;

static voice_t voices[3];
static unsigned char music_on;
static unsigned char filter_mask;
static unsigned char cutoff;
static signed char cutoff_dir;
static unsigned char lfo_tick;
static volatile unsigned char restart_request;

// Effects: { attack/decay, sustain/release, then (control, pitch high byte)
// a frame each, then 0 }
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

static const unsigned char * const sfx_table[] = {
    sfx_jump, sfx_wall_jump, sfx_death, sfx_trophy, sfx_clear, sfx_thunder, sfx_start, sfx_land
};

#define NO_SFX 0xFF
static volatile unsigned char sfx_request = NO_SFX;
static const unsigned char *sfx_pos;

static void song_start(void) {
    unsigned char v, i;

    for (i = 0; i < 0x15; i++) SID[i] = 0;
    for (v = 0; v < 3; v++) {
        voices[v].section = 0;
        voices[v].pos = song_streams[0][v];
        voices[v].left = 1;
        voices[v].note = 0;
        voices[v].inst = song_instruments;
        voices[v].ctrl = 0;
    }
    filter_mask = 0;
}

void sound_init(void) {
    song_start();
    cutoff = FC_MIN;
    cutoff_dir = 1;
    SID[SID_RES_FILT] = RESONANCE;
    SID[SID_MODE_VOL] = 0x1F;   // low-pass, full volume
    music_on = 1;
}

void sound_play(unsigned char sfx) {
    sfx_request = sfx;
}

void sound_music(unsigned char on) {
    music_on = on;
}

void sound_restart(void) {
    restart_request = 1;
}

// The voice's next event, moving on to the next section (or back to the
// loop) at the end of this one
static const unsigned char *next_pos(voice_t *m, unsigned char v) {
    const unsigned char *p = m->pos;
    unsigned char s;

    if (*p == SONG_END) {
        s = m->section + 1;
        if (s >= song_sections) s = song_loop;
        m->section = s;
        p = song_streams[s][v];
        m->pos = p;
    }
    return p;
}

static void start_note(voice_t *m, unsigned char v, unsigned char note, unsigned char inst) {
    const instrument_t *in = song_instruments + inst;
    unsigned char reg = v * 7, bit = 1 << v;

    m->inst = in;
    m->note = note;
    m->age = 0;
    m->arp_pos = 0;
    m->pw = in->pw;
    m->pwm = in->pwm;
    m->freq = in->fixed ? in->fixed : sid_freq[note - 1];
    m->ctrl = in->wave | GATE;
    if (in->filter) filter_mask |= bit;
    else filter_mask &= ~bit;
    if (v == SFX_VOICE && sfx_pos) return;
    SID[reg + V_AD] = in->ad;
    SID[reg + V_SR] = in->sr;
}

static void next_event(voice_t *m, unsigned char v) {
    const unsigned char *p = next_pos(m, v);
    unsigned char note = p[0];

    m->left = p[2];
    m->pos = p + 3;
    if (note == SONG_HOLD) return;
    if (!note) {
        m->ctrl &= ~GATE;
        m->note = 0;
        return;
    }
    start_note(m, v, note, p[1]);
}

static void voice_tick(unsigned char v) {
    voice_t *m = voices + v;
    const instrument_t *in;
    unsigned char reg = v * 7;
    unsigned char note, phase;
    unsigned int freq;

    if (!--m->left) {
        next_event(m, v);
    } else if (m->left == 1 && next_pos(m, v)[0] != SONG_HOLD) {
        // Let go a frame early, so the next note restarts its envelope
        m->ctrl &= ~GATE;
    }
    if (!m->note) {
        if (!(v == SFX_VOICE && sfx_pos)) SID[reg + V_CTRL] = music_on ? m->ctrl : 0;
        return;
    }

    in = m->inst;
    if (m->age < 255) m->age++;
    if (in->gate && m->age == in->gate) m->ctrl &= ~GATE;
    if (in->wave2_at && m->age == in->wave2_at) m->ctrl = in->wave2 | (m->ctrl & GATE);

    if (in->fixed || in->slide) {
        // Drums: a set pitch, falling
        freq = m->freq;
        if (in->slide) {
            freq += in->slide;
            if (in->slide < 0 && (freq < SLIDE_FLOOR || freq > m->freq)) freq = SLIDE_FLOOR;
            m->freq = freq;
        }
    } else {
        note = m->note;
        if (in->arp_n) {
            note += in->arp[m->arp_pos];
            if (++m->arp_pos >= in->arp_n) m->arp_pos = 0;
            if (note > 84) note = 84;
        }
        freq = sid_freq[note - 1];
        if (in->vibrato && m->age > VIBRATO_DELAY) {
            // A triangle wave, 8 frames round, either side of the note
            phase = m->age & 7;
            if (phase & 4) phase = 8 - phase;
            freq += (freq >> 9) * phase - (freq >> 8);
        }
    }

    if (m->pwm) {
        m->pw += m->pwm;
        if (m->pw >= PW_MAX) m->pwm = -(signed char)(m->pwm > 0 ? m->pwm : -m->pwm);
        else if (m->pw <= PW_MIN) m->pwm = m->pwm > 0 ? m->pwm : -m->pwm;
    }

    if (v == SFX_VOICE && sfx_pos) return;
    SID[reg + V_FREQ_LO] = (unsigned char)freq;
    SID[reg + V_FREQ_HI] = (unsigned char)(freq >> 8);
    SID[reg + V_PW_LO] = (unsigned char)m->pw;
    SID[reg + V_PW_HI] = (unsigned char)(m->pw >> 8);
    SID[reg + V_CTRL] = music_on ? m->ctrl : 0;
}

static void sfx_tick(void) {
    unsigned char n = sfx_request;
    voice_t *m = voices + SFX_VOICE;

    if (n != NO_SFX) {
        sfx_request = NO_SFX;
        sfx_pos = sfx_table[n];
        SID[SFX_REG + V_CTRL] = 0;
        SID[SFX_REG + V_AD] = sfx_pos[0];
        SID[SFX_REG + V_SR] = sfx_pos[1];
        SID[SFX_REG + V_PW_LO] = 0x00;
        SID[SFX_REG + V_PW_HI] = 0x08;
        sfx_pos += 2;
    }
    if (!sfx_pos) return;

    if (!sfx_pos[0]) {
        // Done: the music's voice C picks up from its next note
        sfx_pos = nullptr;
        SID[SFX_REG + V_AD] = m->inst->ad;
        SID[SFX_REG + V_SR] = m->inst->sr;
        m->ctrl &= ~GATE;
        return;
    }
    SID[SFX_REG + V_FREQ_LO] = 0;
    SID[SFX_REG + V_FREQ_HI] = sfx_pos[1];
    SID[SFX_REG + V_CTRL] = sfx_pos[0];
    sfx_pos += 2;
}

void sound_tick(void) {
    if (restart_request) {
        restart_request = 0;
        song_start();
    }
    voice_tick(0);
    voice_tick(1);
    voice_tick(2);
    SID[SID_RES_FILT] = RESONANCE | filter_mask;

    // Slowly open and close the filter
    if (++lfo_tick & 1) {
        cutoff += cutoff_dir;
        if (cutoff >= FC_MAX) cutoff_dir = -1;
        else if (cutoff <= FC_MIN) cutoff_dir = 1;
        SID[SID_FC_HI] = cutoff;
    }

    sfx_tick();
}
