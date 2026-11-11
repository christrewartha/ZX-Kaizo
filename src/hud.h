#ifndef HUD_H
#define HUD_H

// Status area below the play area (character rows 22-23)
#define HUD_ROW 22

// Print text at a character cell using the ROM font. Bitmap only: the
// colour comes from the cells' attributes.
void print_at(unsigned char col, unsigned char row, const char *text);

// Write n as 'digits' decimal digits (leading zeros) plus a terminator
char *format_number(char *out, unsigned int n, unsigned char digits);

// Draw the HUD for a newly entered level: its name, centred, over the
// level number
void hud_room(const char *name, unsigned char level, unsigned char count);

// Update the death count on the status line
void hud_deaths(unsigned int deaths);

// Update the trophy count (trophies won of 'count'), and the cup that shows
// whether this level's trophy is being carried
void hud_trophies(unsigned char won, unsigned char count, unsigned char held);

#endif
