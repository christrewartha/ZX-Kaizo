#ifndef SONG_H
#define SONG_H

// The soundtrack's data, compiled from c64/data/music.txt into
// song_data.c by tools/song.ps1, for both the C64 and the Amiga. The
// comments in music.txt say what each instrument setting does.

typedef struct {
    unsigned char wave, wave2, wave2_at;
    unsigned char ad, sr;
    unsigned int pw;
    signed char pwm;            // pulse width change a frame
    unsigned char arp_n;        // chord notes to cycle through, 0 for none
    signed char arp[4];         // semitones above the note
    unsigned char vibrato;
    unsigned char filter;
    unsigned int fixed;         // fixed SID frequency, 0 to play the note
    int slide;                  // added to the frequency every frame
    unsigned char gate;         // frames until let go, 0 to hold
} instrument_t;

// Each section's voices are streams of (note, instrument, frames). Note 0
// is a rest, 1-84 are C1-B7; SONG_HOLD carries on the last note, and
// SONG_END ends the section.
#define SONG_HOLD 0xFE
#define SONG_END  0xFF

extern const instrument_t song_instruments[];
extern const unsigned char * const song_streams[][3];
extern const unsigned char song_sections;
extern const unsigned char song_loop;      // the section to go back to

#endif
