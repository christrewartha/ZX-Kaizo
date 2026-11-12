// The title and ending screens. They borrow the game's display: the logo
// and text are drawn into the front playfield, the towers drift slowly
// behind, the copper paints the sky and shines the logo, and the hero runs
// back and forth along the bottom.
#include "gcc8_c_support.h"
#include "title.h"
#include "video.h"
#include "hud.h"
#include "hw.h"
#include "input.h"
#include "sound.h"
#include "gfx_data.h"

#define LOGO_SCALE  4
#define LOGO_Y      36
#define LOGO_H      (8 * LOGO_SCALE)
#define HERO_Y      176
#define FADE_STEPS  16

static int hero_x, hero_dir;

// Draw text from the system font, each pixel a scale x scale block. The
// big logo gets a bevel (light along its top edges) and a drop shadow.
static void draw_text(int x, int y, const char *text, int scale, unsigned char colour, int bevel) {
    for (int pass = bevel ? 0 : 1; pass < 2; pass++) {
        int off = pass ? 0 : 3;
        for (const char *t = text; *t; t++) {
            const unsigned char *g = hud_glyph(*t);
            int cx = x + (t - text) * 8 * scale;
            for (int r = 0; r < 8; r++) {
                for (int c = 0; c < 8; c++) {
                    if (!(g[r] & (0x80 >> c))) continue;
                    int top = r == 0 || !(g[r - 1] & (0x80 >> c));
                    for (int py = 0; py < scale; py++) {
                        for (int px = 0; px < scale; px++) {
                            unsigned char col = colour;
                            if (!pass) col = 1;                         // shadow
                            else if (bevel && top && py == 0) col = 7;  // lit edge
                            video_plot(cx + c * scale + px + off, y + r * scale + py + off, col);
                        }
                    }
                }
            }
        }
    }
}

static void centre(int y, const char *text, int scale, unsigned char colour) {
    draw_text((VIEW_W - (int)strlen(text) * 8 * scale) / 2, y, text, scale, colour, 0);
}

static void setup(void) {
    video_hero(0, 0, -1);
    video_clear_world();
    video_theme_title(LOGO_Y, LOGO_H);
    video_brightness(0);
    draw_text((VIEW_W - (int)(sizeof(GAME_NAME) - 1) * 8 * LOGO_SCALE) / 2, LOGO_Y, GAME_NAME, LOGO_SCALE, 5, 1);
    hero_x = -32;
    hero_dir = 1;
}

// One frame of the backdrop: the towers drift, the hero runs
static void animate(UWORD t) {
    static const unsigned char run[4] = { HERO_RUN_1, HERO_RUN_2, HERO_RUN_3, HERO_RUN_4 };
    int bg_x = t & 0x7f;

    if (bg_x > 0x3f) bg_x = 0x80 - bg_x;
    video_scroll_both(0, 0, bg_x, 16);

    hero_x += hero_dir * 2;
    if (hero_x > VIEW_W + 16) hero_dir = -1;
    else if (hero_x < -48) hero_dir = 1;
    video_hero(hero_x, HERO_Y, run[(hero_x >> 3) & 3] + (hero_dir < 0));
}

// Fade in, then wait for fire (let go of any held first), then fade out
static void wait_for_fire(const char *top, const char *prompt) {
    UWORD t = 0;
    unsigned char held = 1, shown = 2;

    for (;;) {
        wait_frame();
        t++;
        animate(t);
        if (t <= FADE_STEPS) video_brightness(t);
        if (((t >> 5) & 1) != shown) {
            shown = (t >> 5) & 1;
            hud_message(top, shown ? prompt : "");
        }
        if (input_read() & IN_JUMP) {
            if (!held) break;
        } else {
            held = 0;
        }
    }
    sound_play(SFX_START);
    for (int b = FADE_STEPS - 1; b >= 0; b--) {
        wait_frame();
        animate(++t);
        video_brightness(b);
    }
    video_hero(0, 0, -1);
}

void title_screen(void) {
    sound_restart();
    setup();
    centre(LOGO_Y + LOGO_H + 22, "A KAIZO PLATFORMER", 2, 6);
    centre(LOGO_Y + LOGO_H + 52, "15 LEVELS. NO MERCY.", 1, 7);
    centre(LOGO_Y + LOGO_H + 68, "JOYSTICK IN PORT 2", 1, 4);
    wait_for_fire("A HAUNTED CASTLE AWAITS", "PRESS FIRE TO START");
}

static char *number(char *out, unsigned int n, int digits) {
    out[digits] = 0;
    while (digits) {
        out[--digits] = '0' + n % 10;
        n /= 10;
    }
    return out;
}

void ending_screen(unsigned int deaths, unsigned char trophies, unsigned char count) {
    char line[24], num[5];

    setup();
    centre(LOGO_Y + LOGO_H + 22, "YOU ESCAPED!", 2, 6);
    memcpy(line, "DEATHS ", 7);
    memcpy(line + 7, number(num, deaths > 9999 ? 9999 : deaths, 4), 5);
    centre(LOGO_Y + LOGO_H + 52, line, 1, 7);
    memcpy(line, "TROPHIES ", 9);
    number(line + 9, trophies, 2);
    line[11] = '/';
    number(line + 12, count, 2);
    centre(LOGO_Y + LOGO_H + 68, line, 1, 6);
    wait_for_fire(trophies == count ? "EVERY TROPHY! A TRUE MASTER." : "THE CASTLE IS BEHIND YOU", "PRESS FIRE");
}
