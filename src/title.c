#include <string.h>
#include <compress/zx0.h>
#include "title.h"
#include "render.h"
#include "hud.h"
#include "input.h"
#include "interrupts.h"
#include "music.h"

// Must match $prompt in tools/make_cover.ps1, which draws it on the cover.
// The screen itself (data/title.scr) leaves the bar empty for it.
#define PROMPT     "PRESS SPACE TO START"
#define PROMPT_LEN (sizeof(PROMPT) - 1)
#define PROMPT_COL ((32 - PROMPT_LEN) / 2)

#define ATTR_PROMPT_ON  0x4E    // bright yellow on blue, like the bar
#define ATTR_PROMPT_OFF 0x49    // blue on blue: hidden
#define BLINK_BIT       0x10    // of frame_count: on and off every 16 frames

#define ENDING_PROMPT     "PRESS SPACE"
#define ENDING_PROMPT_LEN (sizeof(ENDING_PROMPT) - 1)
#define ENDING_PROMPT_COL ((32 - ENDING_PROMPT_LEN) / 2)

#define ATTR_ENDING_TITLE 0x46  // bright yellow on black
#define ATTR_ENDING_TEXT  0x47  // bright white on black
#define ATTR_ENDING_TROPHIES 0x46   // bright yellow on black


// zx0-compressed 6912-byte screen (build/gen/title_scr.c)
extern const unsigned char title_scr[];

// Blink the prompt at 'attr' until a jump is pressed, with the music on.
// A jump still held from before must be let go first.
static void wait_for_jump(unsigned char *attr, unsigned char len) {
    while (input_read() & IN_JUMP) {
        wait_frame();
        music_update();
    }
    while (!(input_read() & IN_JUMP)) {
        wait_frame();
        music_update();
        memset(attr, (frame_count & BLINK_BIT) ? ATTR_PROMPT_OFF : ATTR_PROMPT_ON, len);
    }
}

void title_screen(void) {
    unsigned char *prompt_attr = SCREEN_ATTR + HUD_ROW * 32 + PROMPT_COL;

    // Black on black while unpacking; the attributes come last and reveal
    // the whole screen at once
    memset(SCREEN_ATTR, 0, 768);
    dzx0_standard(title_scr, SCREEN_BITMAP);
    print_at(PROMPT_COL, HUD_ROW, PROMPT);

    wait_for_jump(prompt_attr, PROMPT_LEN);
}

void ending_screen(unsigned int deaths, unsigned char trophies, unsigned char count) {
    char num[5];

    memset(SCREEN_ATTR, 0, 768);
    memset(SCREEN_BITMAP, 0, 6144);
    print_at(8, 6, "YOU HAVE ESCAPED");
    print_at(10, 8, "THE CASTLE!");
    print_at(10, 12, "DEATHS");
    print_at(17, 12, format_number(num, deaths > 9999 ? 9999 : deaths, 4));
    print_at(9, 14, "TROPHIES");
    print_at(18, 14, format_number(num, trophies, 2));
    print_at(20, 14, "/");
    print_at(21, 14, format_number(num, count, 2));
    print_at(ENDING_PROMPT_COL, HUD_ROW, ENDING_PROMPT);
    memset(SCREEN_ATTR + 6 * 32, ATTR_ENDING_TITLE, 3 * 32);
    memset(SCREEN_ATTR + 12 * 32, ATTR_ENDING_TEXT, 32);
    memset(SCREEN_ATTR + 14 * 32, ATTR_ENDING_TROPHIES, 32);
    memset(SCREEN_ATTR + HUD_ROW * 32, ATTR_PROMPT_OFF, 32);

    wait_for_jump(SCREEN_ATTR + HUD_ROW * 32 + ENDING_PROMPT_COL, ENDING_PROMPT_LEN);
}
