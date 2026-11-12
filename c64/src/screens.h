#ifndef SCREENS_H
#define SCREENS_H

// Show the title screen until a jump is pressed
void title_screen(void);

// After the last level: the death count and trophies won (of 'count'),
// until a jump is pressed
void ending_screen(unsigned int deaths, unsigned char trophies, unsigned char count);

#endif
