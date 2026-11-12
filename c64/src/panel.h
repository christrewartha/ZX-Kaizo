#ifndef PANEL_H
#define PANEL_H

// The HUD: a stone ledge under the play area (row 22), the level name
// (row 23) and the level, trophy and death counts (row 24)
#define PANEL_ROW 22

void panel_level(const char *name, unsigned char level, unsigned char count);
void panel_deaths(unsigned int deaths);
void panel_trophies(unsigned char won, unsigned char count, unsigned char held);

// Per frame: a glint runs along the level name now and then, and the cup
// flashes while a trophy is carried
void panel_animate(void);

#endif
