; Masked 16x16 sprites using pre-shifted data.
;
; A 16 pixel wide sprite at an arbitrary x spans 3 screen bytes per line.
; Pre-shifted data is 16 lines of {mask,gfx} x 3 = 96 bytes per shift.
; Each screen byte becomes (screen AND mask) OR gfx, and the original byte
; is kept in the sprite's slot so the sprite can be erased without
; redrawing the room. The attribute cells it covers get its ink, and are
; kept in the slot too.
;
; The sprites are done one at a time, top to bottom, and each one is moved
; a pixel line at a time, top to bottom: put back what was under the old
; picture on this line, then draw this line of the new one. Wherever the
; beam is, it shows the old picture or the new one (or the top of one and
; the bottom of the other), never a gap. Erasing everything first and then
; drawing, as this used to, took long enough that sprites high on the
; screen had gone by the time the beam got there.
;
; That only works while no two sprites overlap, now or last frame. When
; they do, they are all erased first, in reverse draw order, as each one's
; saved background holds the ones drawn before it, and then drawn.
;
; It's all in assembly, as in C it was far too slow.

    SECTION code_user

    PUBLIC _sprites_render
    PUBLIC _sprites_reset

    EXTERN _sprites         ; sprite_t[MAX_SPRITES], render.h
    EXTERN _sprite_count
    EXTERN _screen_row      ; unsigned int[192]: address of each pixel line
    EXTERN _spr_shift_ptr   ; unsigned char *[frame][x & 7]: sprite data

defc MAX_SPRITES    = 5     ; as in render.h

; sprite_t in render.h
defc SPRITE_VISIBLE = 4
defc SPRITE_SIZE    = 5

; What each sprite drew last frame. Two buffers for what's under it: one
; holds what's under the old picture while the new one saves into the
; other.
defc SLOT_SCR       = 0     ; top-left screen byte, 0 if nothing is drawn
defc SLOT_SAVE      = 2     ; the buffer with what's under it
defc SLOT_Y         = 4     ; top pixel line
defc SLOT_ATTR      = 5     ; top-left attribute cell, 0 if none painted
defc SLOT_ATTR_SAVE = 7     ; 9 bytes: the attributes painted over
defc SLOT_ROWS      = 16    ; attribute cells covered: 2 or 3 down
defc SLOT_COLS      = 17    ; and 2 or 3 across
defc SLOT_COL       = 18    ; the cells covered
defc SLOT_ROW       = 19
defc SLOT_ROW_END   = 20
defc SLOT_ORDER     = 21    ; place in the drawing order
defc SLOT_BUF0      = 22    ; 48 bytes
defc SLOT_BUF1      = 70    ; 48 bytes
defc SLOT_SIZE      = 118

; Working record for each sprite in sprites_render
defc REC_SLOT       = 0     ; 2 bytes
defc REC_SPRITE     = 2     ; 2 bytes
defc REC_STATE      = 4     ; STATE_ bits
defc REC_TOP        = 5     ; y, to go top to bottom
defc REC_COL        = 6     ; the cells covered now and last frame
defc REC_COL_END    = 7     ; together. A sprite is drawn into 3 screen
defc REC_ROW        = 8     ; bytes, so it covers 3 columns of cells.
defc REC_ROW_END    = 9
defc REC_SIZE       = 10

defc STATE_DONE     = 0     ; nothing (more) to do
defc STATE_DRAWN    = 1     ; bit 0: drawn last frame, so erase it
defc STATE_VISIBLE  = 2     ; bit 1: draw it now

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

; Put back one line from under the old picture, with the alternate
; registers: HL' = save buffer, DE' = screen. Not LDI for the third byte:
; from column 31 its 16-bit INC DE would carry into D and corrupt the
; address. LDI's BC' count doesn't matter.
MACRO OLD_LINE
    exx
    ldi
    ldi
    ld a,(hl)
    ld (de),a
    inc hl
    dec e
    dec e
    ex de,hl
    NEXT_LINE_HL
    ex de,hl
    exx
ENDM

; Draw one line of the new picture, keeping what was there: HL = screen,
; DE = save buffer, SP = sprite data, read by POPping {mask, gfx} pairs
; straight into C and B - much faster than walking a second pointer.
MACRO NEW_LINE
    ld a,(hl)
    ld (de),a
    inc de
    pop bc
    and c
    or b
    ld (hl),a
    inc l
    ld a,(hl)
    ld (de),a
    inc de
    pop bc
    and c
    or b
    ld (hl),a
    inc l
    ld a,(hl)
    ld (de),a
    inc de
    pop bc
    and c
    or b
    ld (hl),a
    dec l
    dec l
    NEXT_LINE_HL
ENDM

_sprites_render:
    push ix
    push iy

    ; A record for each sprite: what it has to do, and where
    ld ix,recs
    ld hl,slots
    ld de,_sprites
    ld b,0                  ; sprite number
build:
    ld (ix+REC_SLOT),l
    ld (ix+REC_SLOT+1),h
    ld (ix+REC_SPRITE),e
    ld (ix+REC_SPRITE+1),d
    ld c,STATE_DONE
    ld a,(hl)               ; drawn last frame if slot->scr
    inc hl
    or (hl)
    dec hl
    jr z,build_undrawn
    ld c,STATE_DRAWN
build_undrawn:
    ld a,(_sprite_count)    ; visible if in use and sprite->visible
    cp b
    jr z,build_hidden
    jr c,build_hidden
    push hl
    ld hl,SPRITE_VISIBLE
    add hl,de
    ld a,(hl)
    pop hl
    or a
    jr z,build_hidden
    set 1,c                 ; STATE_VISIBLE
build_hidden:
    ld (ix+REC_STATE),c
    bit 1,c
    jr z,build_old_box

    ; The new box from the sprite...
    ld a,(de)               ; x
    rrca
    rrca
    rrca
    and $1F
    ld (ix+REC_COL),a
    ld (ix+REC_COL_END),a
    inc de
    ld a,(de)               ; y
    dec de
    ld (ix+REC_TOP),a
    push af
    rrca
    rrca
    rrca
    and $1F
    ld (ix+REC_ROW),a
    pop af
    add a,15
    rrca
    rrca
    rrca
    and $1F
    ld (ix+REC_ROW_END),a
    bit 0,c
    jr z,build_box_done

    ; ...widened to take in the old one
    push hl
    push bc
    ld bc,SLOT_COL
    add hl,bc
    ld a,(hl)
    cp (ix+REC_COL)
    jr nc,build_col_ok
    ld (ix+REC_COL),a
build_col_ok:
    cp (ix+REC_COL_END)
    jr c,build_col_end_ok
    ld (ix+REC_COL_END),a
build_col_end_ok:
    inc hl
    ld a,(hl)
    cp (ix+REC_ROW)
    jr nc,build_row_ok
    ld (ix+REC_ROW),a
build_row_ok:
    inc hl
    ld a,(hl)
    cp (ix+REC_ROW_END)
    jr c,build_row_end_ok
    ld (ix+REC_ROW_END),a
build_row_end_ok:
    pop bc
    pop hl
    jr build_box_done

build_old_box:
    bit 0,c
    jr z,build_next
    push hl                 ; just the old box from the slot
    push bc
    ld bc,SLOT_COL
    add hl,bc
    ld a,(hl)
    ld (ix+REC_COL),a
    ld (ix+REC_COL_END),a
    inc hl
    ld a,(hl)
    ld (ix+REC_ROW),a
    add a,a
    add a,a
    add a,a
    ld (ix+REC_TOP),a
    inc hl
    ld a,(hl)
    ld (ix+REC_ROW_END),a
    pop bc
    pop hl

build_box_done:
    inc (ix+REC_COL_END)
    inc (ix+REC_COL_END)
build_next:
    push bc
    ld bc,SLOT_SIZE
    add hl,bc
    ex de,hl
    ld bc,SPRITE_SIZE
    add hl,bc
    ex de,hl
    ld bc,REC_SIZE
    add ix,bc
    pop bc
    inc b
    ld a,b
    cp MAX_SPRITES
    jp nz,build

    ; Do any two overlap?
    ld ix,recs
    ld b,MAX_SPRITES - 1
overlap_outer:
    ld a,(ix+REC_STATE)
    or a
    jr z,overlap_outer_next
    push ix
    pop iy
    ld c,b
overlap_inner:
    ld de,REC_SIZE
    add iy,de
    ld a,(iy+REC_STATE)
    or a
    jr z,overlap_inner_next
    ld a,(iy+REC_COL_END)
    cp (ix+REC_COL)
    jr c,overlap_inner_next
    ld a,(ix+REC_COL_END)
    cp (iy+REC_COL)
    jr c,overlap_inner_next
    ld a,(iy+REC_ROW_END)
    cp (ix+REC_ROW)
    jr c,overlap_inner_next
    ld a,(ix+REC_ROW_END)
    cp (iy+REC_ROW)
    jr nc,erase_all
overlap_inner_next:
    dec c
    jr nz,overlap_inner
overlap_outer_next:
    ld de,REC_SIZE
    add ix,de
    djnz overlap_outer
    jr do_all

    ; They do: erase everything drawn, the last drawn first
erase_all:
    ld ix,recs
    ld iy,0                 ; the record to erase next
    ld b,MAX_SPRITES
erase_find:
    bit 0,(ix+REC_STATE)
    jr z,erase_find_next
    ld l,(ix+REC_SLOT)
    ld h,(ix+REC_SLOT+1)
    ld de,SLOT_ORDER
    add hl,de
    ld e,(hl)
    ld a,iyh
    or iyl
    jr z,erase_take         ; the first one found
    ld a,e
    cp c
    jr c,erase_find_next
erase_take:
    ld c,e                  ; the highest order so far
    push ix
    pop iy
erase_find_next:
    ld de,REC_SIZE
    add ix,de
    djnz erase_find
    ld a,iyh
    or iyl
    jr z,do_all
    ld a,(iy+REC_STATE)     ; just erase it for now
    push af
    ld (iy+REC_STATE),STATE_DRAWN
    call do_sprite
    pop af
    and STATE_VISIBLE
    ld (iy+REC_STATE),a
    jr erase_all

    ; Then top to bottom: each sprite left to do
do_all:
    xor a
    ld (order),a
do_next:
    ld ix,recs
    ld iy,0                 ; the topmost record still to do
    ld b,MAX_SPRITES
do_find:
    ld a,(ix+REC_STATE)
    or a
    jr z,do_find_next
    ld a,iyh
    or iyl
    jr z,do_take            ; the first one found
    ld a,(ix+REC_TOP)
    cp c
    jr nc,do_find_next
do_take:
    ld c,(ix+REC_TOP)
    push ix
    pop iy
do_find_next:
    ld de,REC_SIZE
    add ix,de
    djnz do_find
    ld a,iyh
    or iyl
    jr z,render_done
    call do_sprite
    ld (iy+REC_STATE),STATE_DONE
    jr do_next

render_done:
    pop iy
    pop ix
    ret

; Erase, draw or move the sprite of the record at IY, as its state says
do_sprite:
    ld l,(iy+REC_SLOT)
    ld h,(iy+REC_SLOT+1)
    ld (slot),hl
    xor a                   ; no old lines, no new lines
    ld (old_lines),a
    ld (new_lines),a

    ; The old picture: put back its attributes now, and its pixels below
    bit 0,(iy+REC_STATE)
    jr z,do_no_old
    ld e,(hl)               ; SLOT_SCR
    inc hl
    ld d,(hl)
    inc hl
    ld (old_scr),de
    ld e,(hl)               ; SLOT_SAVE
    inc hl
    ld d,(hl)
    inc hl
    ld (old_save),de
    ld a,(hl)               ; SLOT_Y
    ld (old_y),a
    ld a,16
    ld (old_lines),a
    ld hl,(slot)
    call attr_args
    call nz,attr_restore
do_no_old:

    ; The new picture: work out where it goes and paint its attributes
    bit 1,(iy+REC_STATE)
    jr z,do_no_new
    call set_up
    ld a,16
    ld (new_lines),a
do_no_new:

    call move_lines

    ; Keep what's needed to undo it next frame
    ld hl,(slot)
    bit 1,(iy+REC_STATE)
    jr nz,do_keep
    xor a                   ; nothing drawn now
    ld (hl),a
    inc hl
    ld (hl),a
    ret
do_keep:
    ld de,(new_scr)
    ld (hl),e               ; SLOT_SCR
    inc hl
    ld (hl),d
    inc hl
    ld de,(new_save)
    ld (hl),e               ; SLOT_SAVE
    inc hl
    ld (hl),d
    inc hl
    ld a,(new_y)
    ld (hl),a               ; SLOT_Y
    ld de,SLOT_COL - SLOT_Y
    add hl,de
    ld a,(new_col)
    ld (hl),a               ; SLOT_COL
    inc hl
    ld a,(new_y)
    ld c,a
    rrca
    rrca
    rrca
    and $1F
    ld (hl),a               ; SLOT_ROW
    inc hl
    ld a,c
    add a,15
    rrca
    rrca
    rrca
    and $1F
    ld (hl),a               ; SLOT_ROW_END
    inc hl
    ld a,(order)
    ld (hl),a               ; SLOT_ORDER
    inc a
    ld (order),a
    ret

; Work out the new picture of the record at IY, in new_*, and paint its
; attributes, keeping them in the slot
set_up:
    ld l,(iy+REC_SPRITE)
    ld h,(iy+REC_SPRITE+1)
    ld a,(hl)               ; x
    ld (new_x),a
    rrca
    rrca
    rrca
    and $1F
    ld (new_col),a
    inc hl
    ld a,(hl)               ; y
    ld (new_y),a
    inc hl
    ld e,(hl)               ; frame
    inc hl
    ld a,(hl)               ; colour
    ld (new_colour),a

    ; gfx = spr_shift_ptr[frame][x & 7]
    ld l,e
    ld h,0
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    ld a,(new_x)
    and 7
    add a,a
    ld e,a
    ld d,0
    add hl,de
    ld de,_spr_shift_ptr
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld (new_gfx),de

    ; scr = screen_row[y] + x / 8. A line's low byte is at most $E0, so
    ; adding the column can't carry.
    ld a,(new_y)
    ld l,a
    ld h,0
    add hl,hl
    ld de,_screen_row
    add hl,de
    ld a,(new_col)
    add a,(hl)
    ld e,a
    inc hl
    ld d,(hl)
    ld (new_scr),de

    ; Save under it into the buffer the old picture isn't using
    ld hl,(slot)
    ld de,SLOT_BUF0
    add hl,de
    ex de,hl                ; DE = buffer 0
    ld hl,(old_save)
    ld a,(old_lines)
    or a
    jr z,set_up_buf         ; no old picture: any buffer will do
    or a
    sbc hl,de
    jr nz,set_up_buf        ; the old one is in buffer 1
    ld hl,SLOT_BUF1 - SLOT_BUF0
    add hl,de
    ex de,hl
set_up_buf:
    ld (new_save),de

    ; The attribute cells: 2x2 when cell aligned, up to 3x3 when not
    ld hl,(slot)
    ld de,SLOT_ATTR
    add hl,de
    ld a,(new_colour)
    or a
    jr nz,set_up_attr
    ld (hl),a               ; none painted
    inc hl
    ld (hl),a
    ret
set_up_attr:
    push hl
    ld a,(new_y)
    and $F8
    ld l,a
    ld h,$58 / 4
    add hl,hl
    add hl,hl               ; $5800 + row * 32
    ld a,(new_col)
    add a,l                 ; can't carry: row * 32 + column
    ld e,a
    ld d,h
    pop hl
    ld (hl),e               ; SLOT_ATTR
    inc hl
    ld (hl),d
    ld de,SLOT_ROWS - SLOT_ATTR - 1
    add hl,de
    ld a,(new_y)
    and 7
    ld a,2
    jr z,set_up_rows
    inc a
set_up_rows:
    ld (hl),a               ; SLOT_ROWS
    inc hl
    ld a,(new_x)
    and 7
    ld a,2
    jr z,set_up_cols
    inc a
set_up_cols:
    ld (hl),a               ; SLOT_COLS
    ld hl,(slot)
    call attr_args
    ld a,(new_colour)
    ld c,a
    jp attr_paint

; From the slot at HL: DE = attribute address, HL = attribute save buffer,
; B = cells across, A' = cells down, and the step from the end of one row
; of cells to the start of the next in both loops. Z if the sprite paints
; no attributes.
attr_args:
    ld bc,SLOT_ATTR
    add hl,bc
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,d
    or e
    ret z
    push hl
    ld bc,SLOT_ROWS - SLOT_ATTR_SAVE
    add hl,bc
    ld a,(hl)               ; down
    ex af,af'
    inc hl
    ld b,(hl)               ; across
    ld a,32
    sub b
    ld (paint_skip + 1),a
    ld (restore_skip + 1),a
    pop hl
    or a                    ; NZ: 32 - across is never 0
    ret

; Each cell keeps its paper and takes the ink in C; the old bytes go to the
; save buffer
attr_paint:
    ex de,hl                ; HL = attributes, DE = save buffer
    ex af,af'
paint_row:
    ex af,af'               ; row counter lives in A'
    push bc
paint_cell:
    ld a,(hl)
    ld (de),a
    inc de
    and $38
    or c
    ld (hl),a
    inc hl
    djnz paint_cell
    pop bc
paint_skip:
    ld a,0                  ; self-modified: 32 - across
    add a,l
    ld l,a
    jr nc,paint_next
    inc h
paint_next:
    ex af,af'
    dec a
    jr nz,paint_row
    ret

attr_restore:
    ex af,af'
restore_row:
    ex af,af'
    push bc
    ld c,b
    ld b,0
    ldir
    pop bc
restore_skip:
    ld a,0                  ; self-modified: 32 - across
    add a,e
    ld e,a
    jr nc,restore_next
    inc d
restore_next:
    ex af,af'
    dec a
    jr nz,restore_row
    ret

; Put back old_lines lines from under the old picture and draw new_lines
; lines of the new one, top to bottom across both. On a line both cover,
; the old one goes first, so the new one saves the room, not the sprite.
; The upper picture has some lines to itself, then the two share lines
; (unless they're 16 or more apart), then the lower one finishes.
move_lines:
    ld a,(old_lines)
    or a
    jr z,move_counts        ; only a new picture: 16 lines of it alone
    ld a,(new_lines)
    or a
    jr z,move_counts        ; only an old picture: 16 lines of it alone
    ld a,(old_y)            ; both: how far apart?
    ld c,a
    ld a,(new_y)
    sub c
    ld b,1                  ; B = 1: the old picture is the upper one
    jr nc,move_apart
    neg
    dec b
move_apart:
    cp 16
    jr c,move_overlap
    ld a,16
move_overlap:
    ld (upper_lines),a      ; lines for the upper alone, then the lower
    ld c,a
    ld a,16
    sub c
    ld (both_lines),a
    ld a,b
    ld (old_upper),a
    jr move_go
move_counts:
    ld a,16                 ; one picture: "upper" alone, whichever it is
    ld (upper_lines),a
    xor a
    ld (both_lines),a
    ld a,(old_lines)
    ld (old_upper),a        ; 16 (true) if it's the old one
move_go:
    ld hl,(old_save)
    ld de,(old_scr)
    exx
    ld hl,(new_scr)
    ld de,(new_save)
    di                      ; SP is about to point at the sprite data
    ld (move_sp + 1),sp
    ld sp,(new_gfx)
    ld a,(old_upper)
    or a
    jp z,new_first

old_first:
    ld a,(upper_lines)
    or a
    jp z,old_first_shared
old_first_alone:
    ex af,af'               ; line counter lives in A'
    OLD_LINE
    ex af,af'
    dec a
    jp nz,old_first_alone
old_first_shared:
    ld a,(both_lines)
    or a
    jp z,old_first_lower
old_first_both:
    ex af,af'
    OLD_LINE
    NEW_LINE
    ex af,af'
    dec a
    jp nz,old_first_both
old_first_lower:
    ld a,(new_lines)        ; nothing below if there's no new picture
    or a
    jp z,move_done
    ld a,(upper_lines)
    or a
    jp z,move_done
old_first_new:
    ex af,af'
    NEW_LINE
    ex af,af'
    dec a
    jp nz,old_first_new
    jp move_done

new_first:
    ld a,(upper_lines)
    or a
    jp z,new_first_shared
new_first_alone:
    ex af,af'
    NEW_LINE
    ex af,af'
    dec a
    jp nz,new_first_alone
new_first_shared:
    ld a,(both_lines)
    or a
    jp z,new_first_lower
new_first_both:
    ex af,af'
    OLD_LINE
    NEW_LINE
    ex af,af'
    dec a
    jp nz,new_first_both
new_first_lower:
    ld a,(old_lines)        ; nothing below if there's no old picture
    or a
    jp z,move_done
    ld a,(upper_lines)
    or a
    jp z,move_done
new_first_old:
    ex af,af'
    OLD_LINE
    ex af,af'
    dec a
    jp nz,new_first_old

move_done:
move_sp:
    ld sp,0                 ; self-modified: the caller's stack
    ei
    ret

_sprites_reset:
    ld hl,slots
    ld de,SLOT_SIZE
    ld b,MAX_SPRITES
    xor a
reset_slot:
    ld (hl),a
    inc hl
    ld (hl),a
    dec hl
    add hl,de
    djnz reset_slot
    ret

    SECTION bss_user

slots:
    defs MAX_SPRITES * SLOT_SIZE
recs:
    defs MAX_SPRITES * REC_SIZE
slot:                       ; the slot being done
    defw 0
order:                      ; how many have been drawn this frame
    defb 0
old_lines:                  ; 16 if there's an old picture to erase
    defb 0
old_scr:
    defw 0
old_save:
    defw 0
old_y:
    defb 0
new_lines:                  ; 16 if there's a new picture to draw
    defb 0
new_scr:
    defw 0
new_save:
    defw 0
new_gfx:
    defw 0
new_x:
    defb 0
new_y:
    defb 0
new_col:
    defb 0
new_colour:
    defb 0
upper_lines:                ; lines for the upper picture alone
    defb 0
both_lines:                 ; lines the two pictures share
    defb 0
old_upper:                  ; non-zero if the old picture is the upper one
    defb 0
