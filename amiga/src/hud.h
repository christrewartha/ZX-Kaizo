#ifndef HUD_H
#define HUD_H

#include <exec/types.h>

// The status bar under the play area: a screen of its own, 320x24 in 4
// colours, with the level's name over the level, trophy and death counts.
// The text is in the system font (topaz), copied before the OS goes.

#define HUD_HEIGHT 24

// Allocate the bar and copy the font. Before hw_take().
void hud_init(void);

// Copper moves that switch the display to the bar; video.c puts them at
// the bar's first line. Returns the end of what it wrote.
UWORD *hud_copper(UWORD *c);

void hud_level(const char *name, unsigned char level, unsigned char count);
void hud_deaths(unsigned int deaths);
void hud_trophies(unsigned char won, unsigned char count, unsigned char held);

// Two centred lines instead (the title and ending screens): the first in
// gold. Either may be empty.
void hud_message(const char *top, const char *bottom);

// A character of the font, 8 bytes, top line first
const unsigned char *hud_glyph(char c);

// Per frame: the cup flashes while a trophy is carried
void hud_animate(void);

#endif
