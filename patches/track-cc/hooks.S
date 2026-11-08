@ track-cc - the hook shims: each stands in for the 4-byte instruction (pair) patch.json overwrites, keeps the stock
@ code's registers intact and calls into track-cc.c. KeyStep Pro main firmware 2.5.20; the addresses named here are in
@ fw.h, per model (the comments give the standard build's). Map: docs/TRACK_CC.md.

#include "fw.h"

    .syntax unified
    .thumb
    .section .text.patch_entry, "ax"

@ ---- a knob turned in Control mode: replaces `ldrb r4,[r3,#4] ; cmp r4,#0x7F` at 0x08012B18 (up), 0x08013FE4 (down)
@ in the encoder handler FUN_08012338. r3 = settings + knob + 0x26, r8 = the knob 0..4, r7 = the value it sends.
@ The stock code goes on with the `beq.w` to its Global fallback, so the flags must be those of `cmp r4, #0x7F`.
@ r0-r3 are free there (reloaded before use).
    .global knob_up
    .thumb_func
knob_up:
    push    {r0, r1, r2, r3, r12, lr}
    bl      knob
    pop     {r0, r1, r2, r3, r12, lr}
    cmp     r4, #0x7F
    b.w     FW_UP_RESUME

    .global knob_down
    .thumb_func
knob_down:
    push    {r0, r1, r2, r3, r12, lr}
    bl      knob
    pop     {r0, r1, r2, r3, r12, lr}
    cmp     r4, #0x7F
    b.w     FW_DOWN_RESUME

    .thumb_func
knob:
    push    {r4, lr}
    ldrb    r0, [r3, #4]            @ the replaced load: the knob's Channel setting
    mov     r1, r8
    mov     r2, r7
    bl      tcc_knob                @ the channel to send on (0x7F: the stock Global fallback)
    pop     {r4, lr}
    mov     r4, r0
    bx      lr

@ ---- the Program Change sender FUN_0801A708: replaces its last send, `bl FUN_0801A6E0` (the PC) at 0x0801A86A -----
@ The sender's 5th argument (the track) is at its sp + 0x30; sp here is the sender's.
    .global pc_tail
    .thumb_func
pc_tail:
    push    {r4, lr}
    add     r4, sp, #8              @ the sender's sp
    bl      FW_MIDI_SEND            @ the replaced call: the PC goes out
    ldrb    r0, [r4, #0x30]
    bl      tcc_after_pc
    pop     {r4, pc}

@ ---- the pattern switch FUN_08035404(seq, pattern, track, x, send): replaces `cmp.w fp, #0` at 0x0803555C ---------
@ fp = the send flag: when set, the stock `bne` goes to the PC block at 0x080355E0, else on to 0x08035562.
    .global sw_check
    .thumb_func
sw_check:
    cmp     fp, #0
    beq     1f
    push    {r0, r1, r2, r3, r12, lr}
    bl      tcc_switch_begin
    pop     {r0, r1, r2, r3, r12, lr}
    b.w     FW_PC_BLOCK
1:  b.w     FW_MERGE

@ ---- the switch after its PC block: replaces `ldr r3, =project ; ldrb r4, [r3]` at 0x08035562 ---------------------
@ r6 = the track, sb = the pattern now loaded. The stock code reloads r0-r3 before use.
    .global sw_merge
    .thumb_func
sw_merge:
    push    {r0, r1, r2, r3, r12, lr}
    mov     r0, r6
    mov     r1, r9
    bl      tcc_after_switch
    pop     {r0, r1, r2, r3, r12, lr}
    movw    r3, #:lower16:RAM_PROJECT
    movt    r3, #:upper16:RAM_PROJECT
    ldrb    r4, [r3]
    b.w     FW_MERGE_4

@ ---- the program function FUN_080475D8(h, sector): replaces `push {r3-r7, lr} ; mov r4, r1` at its entry -----------
@ r1 = the sector about to be erased and programmed from the staging buffer; lr = the caller's return.
    .global save_hook
    .thumb_func
save_hook:
    push    {r0, r1, r2, r3, r12, lr}
    mov     r0, r1
    mov     r1, lr
    bl      tcc_save
    pop     {r0, r1, r2, r3, r12, lr}
    push    {r3, r4, r5, r6, r7, lr}
    mov     r4, r1
    b.w     FW_PROG_4

@ ---- the project load FUN_08046858(h, src, dst): replaces its first instruction, a push.w with lr ---------------------
@ The load runs unchanged as a subroutine of the shim; then our block of project src goes into dst's RAM.
    .global load_hook
    .thumb_func
load_hook:
    push    {r0, r1, r2, lr}
    bl      load_orig
    str     r0, [sp]                @ its result, returned as is
    ldr     r0, [sp, #4]
    ldr     r1, [sp, #8]
    bl      tcc_load
    pop     {r0, r1, r2, pc}

    .thumb_func
load_orig:
#if KSP_CHROMA
    push.w  {r4, r5, r6, r7, r8, r9, r10, r11, lr}
#else
    push.w  {r4, r5, r6, r7, r8, lr}
#endif
    b.w     FW_LOAD_4

@ ---- Copy Project: replaces `bl FUN_08047C84` (the project save, h, src, dst) at 0x0802418C -------------------------
    .global copy_hook
    .thumb_func
copy_hook:
    push    {r0, r1, r2, lr}
    mov     r0, r1
    mov     r1, r2
    bl      tcc_copy
    pop     {r0, r1, r2, lr}
    b.w     FW_PROJ_SAVE
