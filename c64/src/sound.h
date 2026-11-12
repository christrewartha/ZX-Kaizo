#ifndef SOUND_H
#define SOUND_H

// The SID plays c64/data/music.txt: the Spectrum's song as a slow intro,
// then a fast main theme with drums, which loops. Sound effects take over
// the third voice while they play.

#define SFX_JUMP      0
#define SFX_WALL_JUMP 1
#define SFX_DEATH     2
#define SFX_TROPHY    3
#define SFX_CLEAR     4
#define SFX_THUNDER   5
#define SFX_START     6
#define SFX_LAND      7

void sound_init(void);

// Start a sound effect (it replaces any already playing)
void sound_play(unsigned char sfx);

// Start the song again from its intro
void sound_restart(void);

// Turn the music on or off; effects still play
void sound_music(unsigned char on);

// Called by the frame interrupt, 50 times a second
void sound_tick(void);

#endif
