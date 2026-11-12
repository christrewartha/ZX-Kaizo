#ifndef HW_H
#define HW_H

#include <exec/types.h>
#include <hardware/custom.h>

extern volatile struct Custom *custom;

// Frames (50Hz vertical blanks) since hw_take()
extern volatile UWORD frame_count;

// Set by wait_frame() when the frame had already begun: the last frame's
// work overran
extern UBYTE frame_late;

// Find the OS libraries. Call first, then allocate memory, then hw_take().
void hw_init(void);

// Shut the system down and take over the hardware, with our own vertical
// blank interrupt.
void hw_take(void);

// Hand the machine back to the OS
void hw_free(void);

// Wait for the next vertical blank; returns how many have passed since
// the last call (1 unless the game fell behind)
UWORD wait_frame(void);

// Wait for the blitter to finish (straight from the hardware: the OS is off)
static inline void wait_blit(void) {
    (void)custom->dmaconr;          // the first read can be early on some machines
    while (custom->dmaconr & (1 << 14)) {}
}

// Left mouse button (port 1 fire): quits, for development
int mouse_left(void);

#endif
