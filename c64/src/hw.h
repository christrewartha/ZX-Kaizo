#ifndef HW_H
#define HW_H

#include <c64/vic.h>

// Memory map. The VIC looks at bank 3 ($C000-$FFFF), which oscar64 leaves
// free: code and data run from $0801, the stack ends at $A000.
#define SCREEN       ((unsigned char *)0xC000)
#define CHARSET      ((unsigned char *)0xC800)
#define SPRITE_RAM   ((unsigned char *)0xE000)  // under the KERNAL ROM: the
                                                // CPU writes reach the RAM
#define COLOUR_RAM   ((unsigned char *)0xD800)
#define SPRITE_PTRS  (SCREEN + 0x3F8)
#define SPRITE_PTR0  0x80                       // pointer of $E000 in bank 3

#define SCREEN_COLS  40
#define SCREEN_ROWS  25

// The 32 x 22 play area sits between castle walls 4 columns wide
#define PLAY_COL     4
#define WALL_COLS    4

// $D016: multicolour on, 40 columns. $D011: screen on, 25 rows, and the
// usual vertical scroll of 3.
#define D016_NORMAL  0x18
#define D011_NORMAL  0x1B

// Raster line where character row 'row' starts, at the usual scroll
#define ROW_LINE(row) (51 + (row) * 8)

// Sprite registers: the game's 16x16 box at (x, y) in the play area to
// the VIC's sprite coordinates. The art starts 2 pixels in and 1 row down.
#define SPR_X0       (24 + PLAY_COL * 8 - 2)
#define SPR_Y0       (50 - 1)

#define POKE(a, v)   (*(volatile unsigned char *)(a) = (v))
#define PEEK(a)      (*(volatile unsigned char *)(a))

#define CIA1_PRA     0xDC00
#define CIA1_PRB     0xDC01
#define CIA1_ICR     0xDC0D
#define CIA2_PRA     0xDD00

#endif
