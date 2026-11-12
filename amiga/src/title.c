// The title and ending screens. They borrow the game's display: a morning
// sky, a strip of grass along the bottom, the logo and text drawn into the
// playfield (the copper shines the logo), and Foxy running back and forth
// with an opossum on Foxy's tail.
#include "gcc8_c_support.h"
#include "title.h"
#include "video.h"
#include "hud.h"
#include "hw.h"
#include "input.h"
#include "sound.h"
#include "roomgfx.h"
#include "gfx_data.h"

// The camera looks at the bottom of the world, where the grass is
#define CAM_Y       CAM_MAX_Y
#define GROUND_Y    (WORLD_H - 32 - CAM_Y)      // on the screen

#define LOGO_SCALE  4
#define LOGO_Y      20
#define LOGO_H      (8 * LOGO_SCALE)
#define FADE_STEPS  16

static int hero_x, hero_dir;

// Draw text from the system font into the world, each pixel a scale x
// scale block, over a dark shadow. The logo also gets a lit top edge.
static void draw_text(int x, int y, const char *text, int scale, unsigned char colour, int bevel) {
    int shadow = scale > 1 ? scale - 1 : 1;

    y += CAM_Y;
    for (int pass = 0; pass < 2; pass++) {
        int off = pass ? 0 : shadow;
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
                            if (!pass) col = PAL_DARKEST;
                            else if (bevel && top && py == 0) col = PAL_LIGHTEST;
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
    video_theme_title(LOGO_Y, LOGO_H);
    video_brightness(0);
    room_title();
    draw_text((VIEW_W - (int)(sizeof(GAME_NAME) - 1) * 8 * LOGO_SCALE) / 2, LOGO_Y, GAME_NAME, LOGO_SCALE, PAL_SHINE, 1);
    hero_x = -32;
    hero_dir = 1;
}

// One frame of the scene: Foxy runs, and the opossum gives chase
static void animate(UWORD t) {
    static const unsigned char run[6] = { HERO_RUN_1, HERO_RUN_2, HERO_RUN_3, HERO_RUN_4, HERO_RUN_5, HERO_RUN_6 };
    int chaser;

    video_scroll(0, CAM_Y);
    hero_x += hero_dir * 2;
    if (hero_x > VIEW_W + 96) hero_dir = -1;
    else if (hero_x < -96) hero_dir = 1;
    video_hero(hero_x, GROUND_Y - 32, run[(t >> 2) % 6] + (hero_dir < 0));

    chaser = hero_x - hero_dir * 56;
    video_bobs_begin();
    video_bob(chaser, WORLD_H - 64, BOB_WALKER_1 + ((t >> 2) % 6) * 2 + (hero_dir > 0));
    video_bobs_end();
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
    centre(LOGO_Y + LOGO_H + 20, "A KAIZO PLATFORMER", 2, PAL_LIGHTEST);
    centre(LOGO_Y + LOGO_H + 50, "15 LEVELS. NO MERCY.", 1, PAL_LIGHTEST);
    centre(LOGO_Y + LOGO_H + 66, "JOYSTICK IN PORT 2", 1, PAL_SHINE);
    wait_for_fire("ART: SUNNY LAND BY ANSIMUZ", "PRESS FIRE TO START");
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
    centre(LOGO_Y + LOGO_H + 20, "YOU MADE IT!", 2, PAL_LIGHTEST);
    memcpy(line, "DEATHS ", 7);
    memcpy(line + 7, number(num, deaths > 9999 ? 9999 : deaths, 4), 5);
    centre(LOGO_Y + LOGO_H + 50, line, 1, PAL_LIGHTEST);
    memcpy(line, "GEMS ", 5);
    number(line + 5, trophies, 2);
    line[7] = '/';
    number(line + 8, count, 2);
    centre(LOGO_Y + LOGO_H + 66, line, 1, PAL_SHINE);
    wait_for_fire(trophies == count ? "EVERY GEM! A TRUE MASTER." : "THE STORM IS BEHIND YOU", "PRESS FIRE");
}
