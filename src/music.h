#ifndef MUSIC_H
#define MUSIC_H

// Stream markers in the generated note data (build/gen/music_data.c)
#define MUSIC_HOLD 0xFE         // carry on the previous note, don't restart it
#define MUSIC_END  0xFF         // loop back to the start

// Non-zero if an AY sound chip was found. Without one there's no music.
extern unsigned char ay_present;

// Look for an AY and start the song. Call once, after interrupts_init().
void music_init(void);

// Advance the song to the current frame and update the AY. Call once a
// frame; it catches up on frames it missed, so the tempo stays steady.
void music_update(void);


#endif
