; Masked 16x16 sprite blitter using pre-shifted data.
;
; A 16 pixel wide sprite at an arbitrary x spans 3 screen bytes per line.
; Pre-shifted data is 16 lines of {mask,gfx} x 3 = 96 bytes per shift.
; Each screen byte becomes (screen AND mask) OR gfx, and the original byte
; is kept in the buffer at _spr_save so the sprite can be erased without
; redrawing the room.

    SECTION code_user

    PUBLIC _spr_draw
    PUBLIC _spr_erase

    EXTERN _spr_scr         ; unsigned char *  top-left screen byte
    EXTERN _spr_gfx         ; unsigned char *  pre-shifted mask/gfx data
    EXTERN _spr_save        ; unsigned char *  48 byte background buffer

; Move HL down one pixel line in the Spectrum's screen layout
MACRO NEXT_LINE_HL
    LOCAL done
    inc h
    ld a,h
    and 7
    jr nz,done
    ld a,l                  ; crossed a character row
    add a,32
    ld l,a
    jr c,done               ; crossed a screen third: H is already right
    ld a,h
    sub 8
    ld h,a
done:
ENDM

; The sprite data is read by pointing SP at it and POPping {mask, gfx}
; pairs straight into C and B - much faster than walking a second pointer.
; Interrupts are off meanwhile, as an interrupt would push onto the data.
_spr_draw:
    di
    ld (draw_sp + 1),sp
    ld hl,(_spr_scr)
    ld de,(_spr_save)
    ld sp,(_spr_gfx)
    ld a,16
draw_line:
    ex af,af'               ; line counter lives in A'

    ld a,(hl)               ; byte 0
    ld (de),a
    inc de
    pop bc
    and c
    or b
    ld (hl),a
    inc l

    ld a,(hl)               ; byte 1
    ld (de),a
    inc de
    pop bc
    and c
    or b
    ld (hl),a
    inc l

    ld a,(hl)               ; byte 2
    ld (de),a
    inc de
    pop bc
    and c
    or b
    ld (hl),a
    dec l
    dec l

    NEXT_LINE_HL

    ex af,af'
    dec a
    jr nz,draw_line
draw_sp:
    ld sp,0                 ; self-modified: the caller's stack
    ei
    ret

; Copy the saved background back. LDI's BC decrement only ever touches C
; (32 copies from $FF), leaving B free as the line counter.
_spr_erase:
    ld de,(_spr_scr)
    ld hl,(_spr_save)
    ld bc,$10FF
erase_line:
    ldi
    ldi
    ld a,(hl)               ; not LDI: from column 31 its 16-bit INC DE
    ld (de),a               ; would carry into D and corrupt the address
    inc hl
    dec e
    dec e

    ex de,hl
    NEXT_LINE_HL
    ex de,hl

    djnz erase_line
    ret
