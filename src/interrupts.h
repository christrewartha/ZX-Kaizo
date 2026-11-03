#ifndef INTERRUPTS_H
#define INTERRUPTS_H

// Frames (50Hz interrupts) since interrupts_init(), wrapping at 256
extern volatile unsigned char frame_count;

// Switch to our own IM2 frame interrupt (interrupts.asm). Must run before
// any SDCC-compiled code can change IY under the ROM's IM1 routine.
extern void interrupts_init(void);

// Wait for the next frame and return how many frame interrupts have passed
// since the last call (1 unless the game fell behind by a whole frame).
extern unsigned char wait_frame(void);

// Set by wait_frame() when the interrupt had already happened: the last
// frame's work overran, so this frame starts late and the display missed one
extern unsigned char frame_late;

#endif
