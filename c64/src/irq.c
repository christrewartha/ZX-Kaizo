#include "irq.h"
#include "hw.h"
#include "sound.h"

split_t splits[MAX_SPLITS];
unsigned char split_count;
volatile unsigned char frame_count;
unsigned char frame_late;

static unsigned char split_next;
static unsigned char last_frame;

__interrupt void irq_split(void) {
    const split_t *s = splits + split_next;

    vic.ctrl2 = s->d016;
    vic.color_back = s->back;
    vic.color_back1 = s->back1;
    vic.color_back2 = s->back2;
    vic.color_border = s->border;
    if (split_next == 0) {
        vic.ctrl1 = s->d011;
        frame_count++;
        sound_tick();
    }
    if (++split_next >= split_count) split_next = 0;
    vic.raster = splits[split_next].line;
}

// The KERNAL has saved A, X and Y; $EA81 restores them and returns
__asm irq_entry {
    lda $d019
    sta $d019
    jsr irq_split
    jmp $ea81
}

void irq_init(void) {
    __asm { sei }
    POKE(CIA1_ICR, 0x7F);       // no timer interrupts
    PEEK(CIA1_ICR);
    *(void **)0x0314 = irq_entry;
    vic.ctrl1 = splits[0].d011 & 0x7F;
    vic.raster = splits[0].line;
    split_next = 0;
    vic.intr_enable = 0x01;     // raster only
    vic.intr_ctrl = 0xFF;
    last_frame = frame_count;
    __asm { cli }
}

unsigned char wait_frame(void) {
    unsigned char n = frame_count - last_frame;

    frame_late = n != 0;
    while (!n) n = frame_count - last_frame;
    last_frame += n;
    return n;
}
