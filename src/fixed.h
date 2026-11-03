#ifndef FIXED_H
#define FIXED_H

// 8.8 fixed point: high byte = whole pixels, low byte = 1/256ths of a pixel.
// Positions are unsigned so they cover the full 0..255.996 screen width;
// velocities and accelerations are signed. Adding a signed velocity to an
// unsigned position wraps correctly in two's complement, and the wrap itself
// is how we detect walking off the left edge of the screen.
typedef unsigned int ufix8;
typedef int          sfix8;

#define FIX_WHOLE(f)    ((unsigned char)((f) >> 8))
#define FIX_FROM_INT(n) ((ufix8)(n) << 8)

#endif
