// Taking the machine over from AmigaOS (or AROS), after the pattern in the
// VS Code extension's template: save the OS state, switch off its
// interrupts and DMA, and install our own vertical blank handler.
#include "gcc8_c_support.h"
#include <proto/exec.h>
#include <proto/graphics.h>
#include <graphics/gfxbase.h>
#include <exec/execbase.h>
#include <hardware/dmabits.h>
#include <hardware/intbits.h>
#include "hw.h"
#include "sound.h"

struct ExecBase *SysBase;
struct GfxBase *GfxBase;
volatile struct Custom *custom;

volatile UWORD frame_count;
UBYTE frame_late;

static UWORD saved_intena, saved_dmacon, saved_adkcon;
static APTR saved_irq;
static volatile APTR vbr;      // volatile, or GCC traps the reads at address 0x6c
static struct View *saved_view;
static UWORD last_frame;

static __attribute__((interrupt)) void get_vbr_supervisor(void) {
    __asm__ volatile(".short 0x4e7a, 0x0801");  // movec.l vbr, d0
}

static APTR get_vbr(void) {
    if (SysBase->AttnFlags & AFF_68010) return (APTR)Supervisor((ULONG (*)())get_vbr_supervisor);
    return 0;
}

#define LEVEL3_VECTOR (*(volatile APTR *)((UBYTE *)vbr + 0x6c))

static __attribute__((interrupt)) void vblank_handler(void) {
    custom->intreq = INTF_VERTB;
    custom->intreq = INTF_VERTB;    // twice, for the A4000
    frame_count++;
    sound_tick();
}

void hw_init(void) {
    // Exec's base is at address 4. Through a variable, as GCC warns about
    // reading from a constant address that low.
    struct ExecBase **volatile exec_pointer = (struct ExecBase **)4UL;
    SysBase = *exec_pointer;
    custom = (struct Custom *)0xdff000;
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 0);
}

void hw_take(void) {
    Forbid();
    saved_adkcon = custom->adkconr;
    saved_intena = custom->intenar;
    saved_dmacon = custom->dmaconr;
    saved_view = GfxBase->ActiView;
    LoadView(0);
    WaitTOF();
    WaitTOF();
    OwnBlitter();
    WaitBlit();
    Disable();

    custom->intena = 0x7fff;
    custom->intreq = 0x7fff;
    custom->dmacon = 0x7fff;
    for (int i = 0; i < 32; i++) custom->color[i] = 0;

    vbr = get_vbr();
    saved_irq = LEVEL3_VECTOR;
    LEVEL3_VECTOR = (APTR)vblank_handler;
    custom->intena = INTF_SETCLR | INTF_INTEN | INTF_VERTB;
    last_frame = frame_count;
}

void hw_free(void) {
    sound_stop();
    WaitBlit();
    custom->intena = 0x7fff;
    custom->intreq = 0x7fff;
    custom->dmacon = 0x7fff;
    LEVEL3_VECTOR = saved_irq;
    custom->cop1lc = (ULONG)GfxBase->copinit;
    custom->cop2lc = (ULONG)GfxBase->LOFlist;
    custom->copjmp1 = 0x7fff;
    custom->intena = saved_intena | 0x8000;
    custom->dmacon = saved_dmacon | 0x8000;
    custom->adkcon = saved_adkcon | 0x8000;
    WaitBlit();
    DisownBlitter();
    Enable();
    LoadView(saved_view);
    WaitTOF();
    WaitTOF();
    Permit();
    CloseLibrary((struct Library *)GfxBase);
}

UWORD wait_frame(void) {
    UWORD n = frame_count - last_frame;

    frame_late = n != 0;
    while (!n) n = frame_count - last_frame;
    last_frame += n;
    return n;
}

int mouse_left(void) {
    return !(*(volatile UBYTE *)0xbfe001 & 0x40);
}
