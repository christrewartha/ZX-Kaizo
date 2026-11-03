#ifndef RENDER_H
#define RENDER_H

#define SCREEN_BITMAP ((unsigned char *)0x4000)
#define SCREEN_ATTR   ((unsigned char *)0x5800)

// Sprite frames. Frames below SPR_FIRST_EVEN are pre-shifted to all 8 pixel
// positions; frames from SPR_FIRST_EVEN on only to the 4 even ones (half the
// memory, and they snap to 2 pixel steps - fine for enemies).

// Player frames are stored facing right; frame + SPR_LEFT faces left
#define SPR_IDLE           0
#define SPR_RUN_A          1
#define SPR_RUN_B          2
#define SPR_JUMP           3
#define SPR_WALL           4    // sliding down a wall on the player's right
#define SPR_FRAMES_PER_DIR 5
#define SPR_LEFT           SPR_FRAMES_PER_DIR
#define SPR_DEAD           10

#define SPR_FIRST_EVEN     11

#define SPR_WALKER_A       11
#define SPR_WALKER_B       12
#define SPR_WALKER_LEFT    2    // walker frame + this faces left
#define SPR_FLYER_A        15
#define SPR_FLYER_B        16

#define SPR_FRAMES         17

// Rightmost x a 16 pixel sprite can be drawn at: its third byte must stay
// in column 31 or it would wrap onto the next line.
#define SPR_MAX_X 239

// Art flag in sprite_frame_art: build this frame mirrored
#define ART_MIRROR 0x80

// Source art: one 16-bit word per line, leftmost pixel in bit 15. Generated
// from data/sprites.txt (build/gen/sprite_art.h has the ART_* indices).
extern const unsigned int sprite_art[][16];
// Art index (| ART_MIRROR) for each SPR_* frame (sprites.c)
extern const unsigned char sprite_frame_art[SPR_FRAMES];

// Address of the first byte of each of the 192 pixel lines
extern unsigned int screen_row[192];

#define MAX_SPRITES 5

typedef struct {
    unsigned char x, y;
    unsigned char frame;
    unsigned char colour;       // 0: take the ink of the cells underneath;
                                // otherwise ink (+ BRIGHT) painted on the cells it covers
    unsigned char visible;
} sprite_t;

// Fill these in each frame; sprites_render() shows them on the next one
extern sprite_t sprites[MAX_SPRITES];
extern unsigned char sprite_count;

// Build the line table and pre-shift all sprite frames. Call once.
void render_init(void);

// Erase every sprite drawn last frame and draw the current set.
// Call straight after the vblank HALT so it runs ahead of the beam.
void sprites_render(void);

// Forget what was drawn (the screen underneath was redrawn)
void sprites_reset(void);

#endif
