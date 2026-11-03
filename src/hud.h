#ifndef HUD_H
#define HUD_H

// Status area below the play area (character rows 22-23)
#define HUD_ROW 22

// Print text at a character cell using the ROM font. Bitmap only: the
// colour comes from the cells' attributes.
void print_at(unsigned char col, unsigned char row, const char *text);

// Draw the HUD for a newly entered room (its name, centred)
void hud_room(const char *name);

#endif
