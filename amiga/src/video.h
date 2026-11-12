#ifndef VIDEO_H
#define VIDEO_H

// The screen: PAL low resolution, 320x256, in dual playfield mode.
//   front playfield  the level, 16x16 tiles (twice the Spectrum's 8x8),
//                    a 512x352 world that scrolls under the screen, double
//                    buffered so the blitter can draw the enemies into it
//   back playfield   a far wall, or towers outdoors, at half the speed
//   colour 0         a copper gradient, the sky behind both
//   sprites 0-3      the hero, 32x32 in 15 colours (two attached pairs)
//   bottom 24 lines  the HUD, a separate screen (hud.c)

#define VIEW_W   320
#define VIEW_H   232
#define WORLD_W  512
#define WORLD_H  352
#define CAM_MAX_X (WORLD_W - VIEW_W)
#define CAM_MAX_Y (WORLD_H - VIEW_H)

// Allocate the screen memory and build the copper list. Before hw_take().
void video_init(void);

// Start the display. After hw_take().
void video_start(void);

// Colours and background for a level
void video_theme(unsigned char level);

// The titles colours, with the logos lines shining from gold to purple
void video_theme_title(int logo_top, int logo_height);

// Fade everything: 0 black to 16 full brightness
void video_brightness(unsigned char level);

// Per frame: lightning in the storm levels. Returns non-zero as a strike
// begins.
int video_animate(void);

// Draw a 16x16 tile (AT_* from gfx_data.h) into a world cell, in both
// buffers
void video_draw_cell(unsigned char col, unsigned char row, unsigned char tile);

// Clear the world to see-through
void video_clear_world(void);

// Set the camera: the world pixel at the screen's top-left. The back
// playfield follows at half speed, or (_both) wherever it's told.
void video_scroll(int cam_x, int cam_y);
void video_scroll_both(int cam_x, int cam_y, int bg_x, int bg_y);

// Plot a pixel (colour 0-7) into the world, in both buffers
void video_plot(int x, int y, unsigned char colour);

// Show the hero's frame (HERO_*) with its 32x32 top-left at (x, y) on
// the screen, or hide it (frame < 0)
void video_hero(int x, int y, int frame);

// Tint the hero: 0 normal, 1 white, 2 red (for dying)
void video_hero_tint(unsigned char tint);

// Enemies: begin a frame of drawing (puts back the level where the last
// enemies in this buffer were), draw each (BOB_* at a world position),
// then end, to show the result from the next frame
void video_bobs_begin(void);
void video_bob(int x, int y, unsigned char frame);
void video_bobs_end(void);

#endif
