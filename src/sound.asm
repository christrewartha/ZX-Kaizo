; Sound hardware: the AY-3-8912 on 128K machines (and AY interfaces for
; the 48K).

    SECTION code_user

    PUBLIC _ay_detect
    PUBLIC _ay_write

    defc AY_SELECT = $FFFD      ; write: register number; read: its value
    defc AY_DATA   = $BFFD      ; write: value for the selected register

; Returns L = 1 if an AY answers. Register 1 (tone A coarse) only keeps 4
; bits, so two values that read back exactly are a real chip: without one
; the port floats, usually reading $FF.
_ay_detect:
    ld l,0
    ld d,$05
    call probe
    ret nz
    ld d,$0A
    call probe
    ret nz
    inc l
    ret
probe:
    ld bc,AY_SELECT
    ld a,1
    out (c),a
    ld b,AY_DATA / 256
    out (c),d
    ld b,AY_SELECT / 256
    in a,(c)
    cp d
    ret

; Fastcall: H = register, L = value
_ay_write:
    ld bc,AY_SELECT
    out (c),h
    ld b,AY_DATA / 256
    out (c),l
    ret
