; IM2 frame interrupt, replacing the ROM's IM1 routine. The ROM routine
; relies on IY pointing at the system variables, which SDCC-compiled code
; doesn't preserve, and it wastes time scanning the keyboard that
; input_read() already polls directly.
;
; The interrupting device's vector byte isn't reliable on a 48K (it's
; whatever is on the floating bus), so the table is 257 copies of the same
; byte: every possible vector lands on IM2_JUMP.

    SECTION code_user

    PUBLIC _interrupts_init
    PUBLIC _wait_frame
    PUBLIC _frame_count
    PUBLIC _frame_late

    defc IM2_TABLE = $FE00      ; $FE00-$FF00, filled with $FD
    defc IM2_JUMP  = $FDFD      ; "jp isr"; the stack must sit below this

_interrupts_init:
    di
    ld hl,IM2_TABLE
    ld de,IM2_TABLE + 1
    ld bc,256
    ld (hl),IM2_JUMP / 256
    ldir
    ld a,$C3                    ; jp
    ld (IM2_JUMP),a
    ld hl,isr
    ld (IM2_JUMP + 1),hl
    ld a,IM2_TABLE / 256
    ld i,a
    xor a
    ld (_frame_count),a
    ld (last_frame),a
    im 2
    ei
    ret

isr:
    push af                     ; INC (HL) changes the flags
    push hl
    ld hl,_frame_count
    inc (hl)
    pop hl
    pop af
    ei
    reti

; Wait for the next frame interrupt and return how many have happened since
; the last call (in L). If the interrupt already came while we were busy,
; the frame overran: set _frame_late and return straight away rather than
; HALTing and throwing away the rest of this frame too.
_wait_frame:
    ld hl,last_frame
    di
    ld a,(_frame_count)
    cp (hl)
    ld a,1
    jr nz,late
    xor a
    ld (_frame_late),a
    ei                          ; EI takes effect after the next instruction,
    halt                        ; so the interrupt can't slip in before HALT
    jr elapsed
late:
    ld (_frame_late),a
elapsed:
    ld a,(_frame_count)
    ei
    ld c,a
    sub (hl)
    ld (hl),c
    ld l,a
    ret

    SECTION bss_user

_frame_count:
    defb 0
_frame_late:
    defb 0
last_frame:
    defb 0
