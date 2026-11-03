#include <string.h>
#include <compress/zx0.h>
#include "title.h"
#include "render.h"
#include "hud.h"
#include "input.h"
#include "interrupts.h"

// Must match $prompt in tools/make_cover.ps1, which draws it on the cover.
// The screen itself (data/title.scr) leaves the bar empty for it.
#define PROMPT     "PRESS SPACE TO START"
#define PROMPT_LEN (sizeof(PROMPT) - 1)
#define PROMPT_COL ((32 - PROMPT_LEN) / 2)

#define ATTR_PROMPT_ON  0x4E    // bright yellow on blue, like the bar
#define ATTR_PROMPT_OFF 0x49    // blue on blue: hidden
#define BLINK_BIT       0x10    // of frame_count: on and off every 16 frames

// zx0-compressed 6912-byte screen (build/gen/title_scr.c)
extern const unsigned char title_scr[];

void title_screen(void) {
    unsigned char *prompt_attr = SCREEN_ATTR + HUD_ROW * 32 + PROMPT_COL;

    // Black on black while unpacking; the attributes come last and reveal
    // the whole screen at once
    memset(SCREEN_ATTR, 0, 768);
    dzx0_standard(title_scr, SCREEN_BITMAP);
    print_at(PROMPT_COL, HUD_ROW, PROMPT);

    // A jump still held from before must be let go first
    while (input_read() & IN_JUMP) wait_frame();
    while (!(input_read() & IN_JUMP)) {
        wait_frame();
        memset(prompt_attr, (frame_count & BLINK_BIT) ? ATTR_PROMPT_OFF : ATTR_PROMPT_ON, PROMPT_LEN);
    }
}
