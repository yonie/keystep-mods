@ filter-midicc - skip an unchanged Bank MSB / Bank LSB / Program Change on a pattern switch.
@
@ KeyStep Pro main firmware 2.5.20 (standard). Thumb-2. Assembled by patches/make_patch.py, which fills in
@ the {NAMES} below: RAM and flash addresses are handed out by the patcher, the firmware addresses are fixed
@ for this firmware (see patch.json). How it hooks the firmware: docs/FILTER_MIDICC.md.
@
@ RAM, in a block of SDRAM the stock firmware never uses (patches/free_space.json). It is not cleared at boot, so
@ nothing is trusted until {MAGIC} holds "IDIM"; the first call after power-on clears the records and sets it:
@   {MAGIC} 4 bytes
@   {REC}   16 channels x 4 bytes: MSB, LSB, PC, marker (0xA5 = this channel's values are known)
@   {FLAG}  1 byte: 0xA5 while the pattern-switch call is running, else 0
@
@ Hooks (4-byte instructions, overwritten by the patcher):
@   0x08035618  BL FUN_0801A708  ->  BL switch_send      (the pattern switch in FUN_08035404)
@   0x0801A7E8  LDR.W r8,[pc,#0x94]  ->  B.W check_and_record   (FUN_0801A708, after the channel and range
@               checks, before the three sends)

@ ---- switch_send: the pattern switch calls the sender through here, with the flag set -------------------
@ FUN_0801A708 takes a 5th argument (the track) on the stack, so the stub re-pushes it before the call.
switch_send:
    push    {r4, lr}
    sub     sp, #8
    ldr     r4, [sp, #16]           @ the caller's 5th argument
    str     r4, [sp]
    movw    ip, #{FLAG_LO}
    movt    ip, #{FLAG_HI}
    movs    r4, #0xA5
    strb    r4, [ip]
    bl      {SENDER}                @ FUN_0801A708, unchanged
    movw    ip, #{FLAG_LO}
    movt    ip, #{FLAG_HI}
    movs    r4, #0
    strb    r4, [ip]
    add     sp, #8
    pop     {r4, pc}

@ ---- check_and_record: inside FUN_0801A708 -------------------------------------------------------------
@ Live here: r7 = MIDI channel (the sender masks it with 0xF), r5 = MSB, r6 = LSB, r4 = PC (wire value 0..127).
@ r0-r3, r8, r9 are free: the code this returns to overwrites them before use.
check_and_record:
    movw    r1, #{MAGIC_LO}
    movt    r1, #{MAGIC_HI}
    ldr     r2, [r1]
    movw    r3, #0x4449             @ "IDIM": the records below were written by us
    movt    r3, #0x4D49
    cmp     r2, r3
    beq     known
    str     r3, [r1]                @ first call since power-on: no channel is known yet
    movw    r1, #{REC_LO}
    movt    r1, #{REC_HI}
    movs    r2, #0
    movs    r3, #64
clear:
    subs    r3, #4
    str     r2, [r1, r3]
    bne     clear
known:
    and     r0, r7, #15
    movw    r1, #{REC_LO}
    movt    r1, #{REC_HI}
    add.w   r1, r1, r0, lsl #2      @ this channel's record
    movw    r2, #{FLAG_LO}
    movt    r2, #{FLAG_HI}
    ldrb    r2, [r2]
    cmp     r2, #0xA5
    bne     record                  @ not the pattern switch (a menu preview): always send
    ldrb    r3, [r1, #3]
    cmp     r3, #0xA5
    bne     record                  @ nothing sent on this channel since boot
    ldrb    r3, [r1]
    cmp     r3, r5
    bne     record
    ldrb    r3, [r1, #1]
    cmp     r3, r6
    bne     record
    ldrb    r3, [r1, #2]
    cmp     r3, r4
    bne     record
    b.w     {EPILOGUE}              @ the channel already has this sound: return without sending
record:
    strb    r5, [r1]
    strb    r6, [r1, #1]
    strb    r4, [r1, #2]
    movs    r3, #0xA5
    strb    r3, [r1, #3]
    movw    r8, #{POOL_LO}          @ the instruction the hook replaced: ldr.w r8, [pc, #0x94],
    movt    r8, #{POOL_HI}          @ i.e. r8 = *(0x0801A880)
    ldr.w   r8, [r8]
    b.w     {SEND}                  @ continue with the three sends
