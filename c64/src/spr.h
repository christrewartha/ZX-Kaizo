#ifndef SPR_H
#define SPR_H

// The eight hardware sprites:
#define SLOT_OUTLINE 0          // the player's hi-res outline, over...
#define SLOT_PLAYER  1          // ...the player's multicolour body
#define SLOT_ENEMY   2          // 2-4: one each, up to 3 enemies
#define SLOT_TROPHY  5
#define SLOT_FX      6          // 6-7: dust and sparkles
#define FX_SLOTS     2

// Shadow registers, written to the VIC by spr_commit(). Positions are
// play-area pixels for the 16x16 game box, as in the game logic; they
// may be off the play area (negative, or past the bottom).
typedef struct {
    int x, y;
    unsigned char image;        // SP_* from build/c64gen/sprite_data.h
    unsigned char colour;
    unsigned char on;
} hwsprite_t;

extern hwsprite_t hw[8];

// Copy the sprite images into place and set the shared colours. Once.
void spr_init(void);

// Hide them all
void spr_hide(void);

// Write the shadows to the VIC, shifted right by 'dx' pixels. Call early
// in the frame, while the beam is in the border.
void spr_commit(unsigned char dx);

#define FX_PUFF  0              // dust: landing, jumping
#define FX_SPARK 1              // sparkles: trophies

// Start a particle animation with its 16x16 box at (x, y)
void fx_spawn(unsigned char kind, int x, int y);

// Advance the particles into their sprite slots. Once a frame.
void fx_update(void);

// Stop them all
void fx_clear(void);

#endif
