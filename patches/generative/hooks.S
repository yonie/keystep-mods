@ generative - the hook shims: each stands in for the 4-byte instruction patch.json overwrites, keeps the stock code's
@ registers intact and calls into generative.c. KeyStep Pro main firmware 2.5.20; the addresses named here are in fw.h,
@ per model (the comments give the standard build's).

#include "fw.h"

    .syntax unified
    .thumb
    .section .text.patch_entry, "ax"

@ ---- button press: replaces `bl FUN_0804AC6E` at 0x080500F4 inside FUN_080500D8(panel, code, 1) --------------------
@ r0 = r5 = the button's record, r1 = 0, r4 = panel, r6 = code. gen_on_press returns 1 when it took the press (the
@ chord): then the stock handler is left through its own epilogue at 0x0805031E (add sp,#0x244; pop {r4-r11,pc}).
    .global gen_press_hook
    .thumb_func
gen_press_hook:
    push    {r0, r1, r2, lr}
    bl      FW_BTN_DOWN             @ the call this hook replaced: mark the button down
    mov     r0, r6
    mov     r1, r4
    bl      gen_on_press
    cmp     r0, #0
    pop     {r0, r1, r2, lr}
    bne     1f
    bx      lr                      @ not ours: on with the stock handler
1:  movw    lr, #:lower16:(FW_PRESS_EPI + 1)
    movt    lr, #:upper16:(FW_PRESS_EPI + 1)
    bx      lr                      @ ours: skip the stock handler

@ ---- the step player: replaces `push.w {r4-r11,lr}` at FUN_08039BDC(seq, track) -----------------------------------
    .global gen_play_hook
    .thumb_func
gen_play_hook:
    push    {r0, r1, r2, r3, r12, lr}
    mov     r0, r1
    bl      gen_is_active
    cmp     r0, #0
    pop     {r0, r1, r2, r3, r12, lr}
    beq     gen_play_stock
    b.w     gen_play                @ generative track: generative.c plays the step (returns to FUN_08039BDC's caller)
gen_play_stock:
    push.w  {r4, r5, r6, r7, r8, r9, r10, r11, lr}
    b.w     FW_PLAY_4

@ C-callable: the stock step player, untouched
    .global gen_play_orig
    .thumb_func
gen_play_orig:
    push.w  {r4, r5, r6, r7, r8, r9, r10, r11, lr}
    b.w     FW_PLAY_4

@ ---- encoder turns: replace `sub.w r8, r6, #3` at 0x08012730 (up) / 0x08013BE6 (down) in FUN_08012338 ----------------
@ r6 = b3 (knob code), [sp+0x6b] = magnitude. r8 = knob index for the stock code, or 0xFF when generative.c took the turn
@ (the stock `cmp r8,#4; bhi` then skips to the next event). Every other register is kept.
    .global gen_turn_up_hook
    .thumb_func
gen_turn_up_hook:
    push    {r0, r1, r2, r3, r12, lr}
    mov     r0, r6
    movs    r1, #1
    ldrb    r2, [sp, #(24 + 0x6b)]
    bl      gen_turn
    b       2f
    .global gen_turn_down_hook
    .thumb_func
gen_turn_down_hook:
    push    {r0, r1, r2, r3, r12, lr}
    mov     r0, r6
    movs    r1, #0
    ldrb    r2, [sp, #(24 + 0x6b)]
    bl      gen_turn
2:  sub.w   r8, r6, #3
    cmp     r0, #0
    it      ne
    movne   r8, #0xFF
    pop     {r0, r1, r2, r3, r12, lr}
    bx      lr

@ ---- encoder touch: replaces `bl FUN_080100BC` (r0 = fp, r1 = b3, r2 = -1, r3 = 0) at 0x0801269C ---------------------
    .global gen_touch_hook
    .thumb_func
gen_touch_hook:
    push    {r0, r1, r2, r3, r12, lr}
    mov     r0, r1
    bl      gen_touch
    cmp     r0, #0
    pop     {r0, r1, r2, r3, r12, lr}
    bne     3f
    b.w     FW_POPUP                @ not ours: the stock popup, returning to the original caller
3:  bx      lr

@ ---- encoder release: replaces `bl FUN_0801F7D8` (r0 = M, r1 = b3) at 0x080125AC -----------------------------------
    .global gen_untouch_hook
    .thumb_func
gen_untouch_hook:
    push    {r0, r1, r2, r3, r12, lr}
    mov     r0, r1
    bl      gen_untouch
    cmp     r0, #0
    pop     {r0, r1, r2, r3, r12, lr}
    bne     4f
    b.w     FW_POPUP_TIMER
4:  bx      lr

@ ---- the main loop's LED flush: replaces `bl FUN_08054C2C` (r0 = the LED object) at 0x0801696A -----------------------
    .global gen_flush_hook
    .thumb_func
gen_flush_hook:
    push    {r4, lr}
    bl      FW_LED_FLUSH
    bl      gen_tick
    pop     {r4, pc}

@ ---- every packet to the UI MCU: replaces `push {r4,lr}; sub sp,#0x10` at FUN_080102E0(port, buf) -------------------
@ gen_tx_filter may change the packet (the track display) or return 0 to drop it (stock step LEDs while ours show).
    .global gen_tx_hook
    .thumb_func
gen_tx_hook:
    push    {r0, r1, r2, r3, r12, lr}
    mov     r0, r1
    bl      gen_tx_filter
    cmp     r0, #0
    pop     {r0, r1, r2, r3, r12, lr}
    beq     5f
    push    {r4, lr}
    sub     sp, #0x10
    b.w     FW_PKT_SEND_4
5:  movs    r0, #0
    bx      lr
