@ generative - letters on the 7-segment track displays. UI MCU firmware 0.0.1 (build 1823), a Cortex-M0: Thumb-1 only.
@
@ The main MCU tells the UI what a track display shows with a group-1 packet: value 0..99 is drawn from the digit
@ table at 0x08027934, 0xFF blanks it, and 100..254 are ignored. FUN_0800B1C4(obj, track, value, dots) does that,
@ and this hook takes the values 0xA0..0xA1 for itself: each picks two letters from the table below. Every other
@ value runs the stock code unchanged. The main-MCU side (generative.c, gen_tx_filter) sends them for a generative track.
@
@ Segments, one byte per digit: bit 0 a (top), 1 b, 2 c, 3 d (bottom), 4 e, 5 f, 6 g (middle), 7 the dot.

    .syntax unified
    .thumb
    .section .text.patch_entry, "ax"

@ ---- replaces `movs r3, r2; subs r3, #0x64` at 0x0800B1D4 in FUN_0800B1C4 ----------------------------------------
@ r2 = value, r4 = dots (bit 0 the ones digit, bit 1 the tens digit). For a letter pair it loads the tens byte into
@ r3 and the ones byte into r7 and returns to the stock write tail at 0x0800B218, which stores them in the display
@ shadow and marks it dirty (r5 = obj, the track at [sp, #4] - untouched here). Clobbers r0 only.
    .equ    NPAIRS, 2               @ the pairs in `letters`, from 0xA0
    .global ui_letters
    .thumb_func
ui_letters:
    movs    r3, r2
    subs    r3, #0xA0
    cmp     r3, #(NPAIRS - 1)
    bhi     stock
    lsls    r3, r3, #1
    adr     r0, letters
    adds    r0, r0, r3
    ldrb    r3, [r0]                @ tens
    ldrb    r7, [r0, #1]            @ ones
    lsls    r0, r4, #31             @ dots, as the stock code draws them
    bpl     1f
    movs    r0, #0x80
    orrs    r7, r0
1:  lsls    r0, r4, #30
    bpl     2f
    movs    r0, #0x80
    orrs    r3, r0
2:  mov     r0, lr                  @ lr = 0x0800B1D9 (the BL at 0x0800B1D4)
    adds    r0, #(0x0800B219 - 0x0800B1D9)
    mov     lr, r0
    bx      lr                      @ -> 0x0800B218: the stock write
stock:
    movs    r3, r2                  @ the two instructions this hook replaced
    subs    r3, #0x64
    bx      lr

    .align  2
letters:
    .byte   0x71, 0x38              @ 0xA0 "FL": Flux
    .byte   0x73, 0x3E              @ 0xA1 "PU": Pulse
