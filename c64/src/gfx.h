#ifndef GFX_H
#define GFX_H

// Set up the VIC: bank 3, multicolour text, our character set (the ROM
// font plus c64/data/chars.txt). Call once, before irq_init().
void gfx_init(void);

// Clear the whole screen to black spaces
void gfx_clear(void);

// A pseudo-random byte (main code only, not the interrupt)
unsigned char rnd(void);

// Screen code for an ASCII character in our character set
unsigned char screen_code(char c);

// Print at a screen cell in one colour (0-7: these are hi-res chars)
void print_at(unsigned char col, unsigned char row, const char *text, unsigned char colour);

// Write n as 'digits' decimal digits (leading zeros) plus a terminator
char *format_number(char *out, unsigned int n, unsigned char digits);

// The game screen's raster splits: play area in the level's colours,
// HUD in its own
void gfx_game_splits(void);

// Per frame: animate the exit, torches and stars, and run the lightning
// and screen shake
void gfx_animate(void);

// Shake the screen for this many frames
void gfx_shake(unsigned char frames);

// Horizontal shake offset this frame, for the sprites to follow
extern unsigned char gfx_shake_x;

// Non-zero for levels with storms: lightning flashes now and then
extern unsigned char gfx_storm;

#endif
