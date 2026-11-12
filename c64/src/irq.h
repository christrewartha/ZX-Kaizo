#ifndef IRQ_H
#define IRQ_H

// The raster interrupt runs a short list of splits each frame. Each one
// sets the scroll register and the three shared background colours from
// a raster line on, so different parts of the screen get their own palette.
// Split 0 is the frame interrupt: it fires in the lower border, sets up the
// top of the next frame (including $D011, for the vertical scroll), counts
// the frame and plays the music. The others must follow in line order.
typedef struct {
    unsigned char line;
    unsigned char d011;         // split 0 only
    unsigned char d016;
    unsigned char back;         // $D021
    unsigned char back1;        // $D022
    unsigned char back2;        // $D023
    unsigned char border;       // $D020
} split_t;

#define MAX_SPLITS  4
#define FRAME_LINE  252         // below the last character row

extern split_t splits[MAX_SPLITS];
extern unsigned char split_count;

// Frames (50Hz) since irq_init(), wrapping at 256
extern volatile unsigned char frame_count;

// Set by wait_frame() when the frame had already started: the last
// frame's work overran
extern unsigned char frame_late;

// Take over the KERNAL's interrupt: raster splits instead of its 60Hz
// timer (which also stops it scanning the keyboard; input.c reads it)
void irq_init(void);

// Wait for the next frame interrupt and return how many have happened
// since the last call (1 unless the game fell behind)
unsigned char wait_frame(void);

#endif
