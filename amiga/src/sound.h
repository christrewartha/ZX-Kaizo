#ifndef SOUND_H
#define SOUND_H

// Paula plays the C64's soundtrack (c64/data/music.txt): the Spectrum's
// song as a slow intro, then the fast theme with drums. The SID's voices
// take channels 0-2, played from generated waveform and drum samples with
// the instruments' envelopes, pulse sweeps, arpeggios and vibrato. Sound
// effects have channel 3 to themselves, so they never cut the music.

#define SFX_JUMP      0
#define SFX_WALL_JUMP 1
#define SFX_DEATH     2
#define SFX_TROPHY    3
#define SFX_CLEAR     4
#define SFX_THUNDER   5
#define SFX_START     6
#define SFX_LAND      7

// Set up and start the song. Before the frame interrupt starts.
void sound_init(void);

// Start a sound effect (it replaces any already playing)
void sound_play(unsigned char sfx);

// Start the song again from its intro
void sound_restart(void);

// Called by the frame interrupt, 50 times a second
void sound_tick(void);

// Silence Paula, before handing the machine back
void sound_stop(void);

#endif
