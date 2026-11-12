#ifndef TITLE_H
#define TITLE_H

// The Amiga version's name, on the title screen
#define GAME_NAME "KAIZAMIGO"

// Show the title until fire is pressed (then fade out)
void title_screen(void);

// After the last level: deaths and trophies won (of 'count'), until fire
void ending_screen(unsigned int deaths, unsigned char trophies, unsigned char count);

#endif
