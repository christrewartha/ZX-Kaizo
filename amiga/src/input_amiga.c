// The Amiga side of src/input.h: the joystick in port 2. Up or fire jumps.
#include "input.h"
#include "hw.h"

// Not used on the Amiga, but src/input.h declares it
unsigned char kempston_on;

void input_init(void) {
}

unsigned char input_read(void) {
    UWORD j = custom->joy1dat;
    unsigned char in = 0;

    // Right and left are bits 1 and 9; down and up are those XORed with
    // the bits below them
    if (j & 0x0002) in |= IN_RIGHT;
    if (j & 0x0200) in |= IN_LEFT;
    if ((j ^ (j >> 1)) & 0x0001) in |= IN_DOWN;
    if ((j ^ (j >> 1)) & 0x0100) in |= IN_UP;
    if (!(*(volatile UBYTE *)0xbfe001 & 0x80)) in |= IN_FIRE;
    return in;
}
