#ifndef INPUT_H
#define INPUT_H

// Input bits deliberately match the Kempston port layout, so the joystick
// value can be used as-is and the keyboard is merged into it.
#define IN_RIGHT 0x01
#define IN_LEFT  0x02
#define IN_DOWN  0x04
#define IN_UP    0x08
#define IN_FIRE  0x10

#define IN_JUMP  (IN_UP | IN_FIRE)

// Non-zero if a Kempston interface was detected at startup
extern unsigned char kempston_on;

// Probe port 0x1F for a Kempston interface (implemented in input.asm)
extern void input_init(void);

// Poll the keyboard half-rows and Kempston port directly - no ROM routines,
// no key repeat, no latency. Keys: O/P left/right, Q/SPACE/M jump, A down.
extern unsigned char input_read(void);

#endif
