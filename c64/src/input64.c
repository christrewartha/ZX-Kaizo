// The C64 side of src/input.h: joysticks in either port, and the same keys
// as on the Spectrum, read straight from CIA 1 (the KERNAL no longer scans
// the keyboard; see irq.c).
#include "input.h"
#include "hw.h"

// Not used on the C64, but src/input.h declares it
unsigned char kempston_on;

// Joystick bits on CIA 1 (active low)
#define JOY_UP    0x01
#define JOY_DOWN  0x02
#define JOY_LEFT  0x04
#define JOY_RIGHT 0x08
#define JOY_FIRE  0x10

void input_init(void) {
    POKE(0xDC02, 0xFF);         // port A drives the keyboard columns
    POKE(0xDC03, 0x00);         // port B reads the rows
}

static unsigned char joy_bits(unsigned char j) {
    unsigned char in = 0;

    j = ~j;
    if (j & JOY_UP)    in |= IN_UP;
    if (j & JOY_DOWN)  in |= IN_DOWN;
    if (j & JOY_LEFT)  in |= IN_LEFT;
    if (j & JOY_RIGHT) in |= IN_RIGHT;
    if (j & JOY_FIRE)  in |= IN_FIRE;
    return in;
}

// Rows pressed in one keyboard column (bits set = pressed)
static unsigned char key_column(unsigned char select) {
    POKE(CIA1_PRA, select);
    return ~PEEK(CIA1_PRB);
}

unsigned char input_read(void) {
    unsigned char in, joy1, joy2, k;

    // With no column selected, port A is joystick 2 and port B joystick 1
    POKE(CIA1_PRA, 0xFF);
    joy2 = PEEK(CIA1_PRA);
    joy1 = PEEK(CIA1_PRB);
    in = joy_bits(joy2) | joy_bits(joy1);

    // A held joystick shares lines with the keyboard matrix and would read
    // as other keys, so only scan the keyboard when both sticks are idle
    if ((joy2 & joy1 & 0x1F) == 0x1F) {
        k = key_column(0xEF);           // column 4: O (row 6), M (4), N (7)
        if (k & 0x40) in |= IN_LEFT;
        if (k & 0x10) in |= IN_FIRE;
        if (k & 0x80) in |= IN_SKIP;
        k = key_column(0xDF);           // column 5: P (row 1)
        if (k & 0x02) in |= IN_RIGHT;
        k = key_column(0x7F);           // column 7: Q (row 6), SPACE (4)
        if (k & 0x40) in |= IN_UP;
        if (k & 0x10) in |= IN_FIRE;
        k = key_column(0xFD);           // column 1: A (row 2)
        if (k & 0x04) in |= IN_DOWN;
        POKE(CIA1_PRA, 0xFF);
    }
    return in;
}
