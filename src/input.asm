; Direct hardware input polling. Every key line is active-low on port $FE,
; with the half-row selected by the high address byte (A for IN A,(n)).

    SECTION code_user

    PUBLIC _input_init
    PUBLIC _input_read

    PUBLIC _kempston_on

; A real Kempston interface always reads 0 in bits 5-7. With no interface the
; port floats (usually $FF), which would otherwise look like every direction
; held at once, so sample it a few times and reject anything with those bits.
_input_init:
    ld b,16
    ld c,0
probe:
    in a,($1f)
    or c
    ld c,a
    djnz probe
    xor a
    bit 7,c
    jr nz,no_kempston
    bit 6,c
    jr nz,no_kempston
    bit 5,c
    jr nz,no_kempston
    inc a
no_kempston:
    ld (_kempston_on),a
    ret

; Returns IN_* bits in L (H = 0)
_input_read:
    ld c,0
    ld a,(_kempston_on)
    or a
    jr z,keys
    in a,($1f)
    and $1f
    ld c,a
keys:
    ld a,$df            ; Y U I O P   (bit 0 = P, bit 1 = O)
    in a,($fe)
    rra
    jr c,no_p
    set 0,c             ; IN_RIGHT
no_p:
    rra
    jr c,no_o
    set 1,c             ; IN_LEFT
no_o:
    ld a,$fb            ; T R E W Q   (bit 0 = Q)
    in a,($fe)
    rra
    jr c,no_q
    set 3,c             ; IN_UP
no_q:
    ld a,$fd            ; G F D S A   (bit 0 = A)
    in a,($fe)
    rra
    jr c,no_a
    set 2,c             ; IN_DOWN
no_a:
    ld a,$7f            ; B N M SYM SPACE   (bit 0 = SPACE, bit 2 = M)
    in a,($fe)
    cpl
    and $05
    jr z,no_fire
    set 4,c             ; IN_FIRE
no_fire:
    ld l,c
    ld h,0
    ret

    SECTION bss_user

_kempston_on:
    defb 0
