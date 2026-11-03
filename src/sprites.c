#include "render.h"
#include "sprite_art.h"

// Which art each SPR_* frame is built from. The art itself lives in
// data/sprites.txt.
const unsigned char sprite_frame_art[SPR_FRAMES] = {
    ART_IDLE, ART_RUN_A, ART_RUN_B, ART_JUMP, ART_WALL,
    ART_IDLE | ART_MIRROR, ART_RUN_A | ART_MIRROR, ART_RUN_B | ART_MIRROR,
    ART_JUMP | ART_MIRROR, ART_WALL | ART_MIRROR,
    ART_DEAD,
    ART_WALKER_A, ART_WALKER_B, ART_WALKER_A | ART_MIRROR, ART_WALKER_B | ART_MIRROR,
    ART_FLYER_A, ART_FLYER_B,
};
