#ifndef ROOMGFX_H
#define ROOMGFX_H

// Amiga extras alongside src/room.h (room_load() and the rest are there)

// Step the exit's animation; call once a frame
void room_animate(unsigned short frame);

// The tile (AT_*) a cell shows now, to draw it again (index as room_coll)
unsigned char room_cell_tile(unsigned int cell);

// The first exit cell (index into room_coll), or -1
int room_exit_cell(void);

#endif
