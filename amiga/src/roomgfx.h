#ifndef ROOMGFX_H
#define ROOMGFX_H

// Amiga extras alongside src/room.h (room_load() and the rest are there)

// Turn the gem; call once a frame
void room_animate(unsigned short frame);


// The exit's door: its bottom-left cell (index into room_coll)
int room_exit_cell(void);

// Open the door (the level is done)
void room_open_door(void);

// The title's world instead of a level: open sky over the ground
void room_title(void);

#endif
