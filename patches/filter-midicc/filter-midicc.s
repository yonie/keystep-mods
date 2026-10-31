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
@   0x0803BCB6  LDRB.W fp,[r4,#0xc]  ->  B.W play_start   (FUN_0803B578(seq, 0xFA), Play: fp = the play flag)
@   0x0803C498  MOVS r5,#0 ; STR r5,[sp,#4]  ->  B.W stop_forget   (FUN_0803B578(seq, 0xFC), Stop)
@   0x080166A4  BL FUN_0803B1FC  ->  BL boot_forget   (the boot constructs the sequencer object, once)
@   0x0801534C  LDR r3,[pc,#0x198] ; LDR r2,[r3]  ->  B.W key_press   (the panel handler's key-press case)

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

@ ---- send_track: send one track's current pattern MSB / LSB / PC, as the pattern switch in FUN_08035404 does --
@ r0 = the sequencer object, r1 = track 0..3, r2 = 1: through switch_send (skipped when the channel already has
@ those values), 0: straight to the sender (always sent; the record still learns it).
send_track:
    push    {r4, r5, r6, r7, r8, r9, r10, lr}
    sub     sp, #8
    mov     r4, r0
    mov     r5, r1
    mov     r9, r2
    movw    r6, #{CURPAT_LO}
    movt    r6, #{CURPAT_HI}
    ldrb    r6, [r6, r5]            @ the track's current pattern
    movw    r7, #{BANK_LO}
    movt    r7, #{BANK_HI}
    ldrb    r7, [r7]
    mov     r0, r4
    mov     r1, r5
    mov     r2, r6
    mov     r3, r7
    bl      {GET_MSB}
    mov     r8, r0
    mov     r0, r4
    mov     r1, r5
    mov     r2, r6
    mov     r3, r7
    bl      {GET_LSB}
    str     r0, [sp, #4]
    mov     r0, r4
    mov     r1, r5
    mov     r2, r6
    mov     r3, r7
    bl      {GET_PC}
    mov     r3, r0
    str     r5, [sp]                @ the sender's 5th argument: the track
    ldr     r2, [sp, #4]
    mov     r1, r8
    movw    r0, #{PORT_LO}
    movt    r0, #{PORT_HI}
    ldr     r0, [r0]
    cmp     r9, #0
    beq     send_always
    bl      switch_send
    b       send_done
send_always:
    bl      {SENDER}
send_done:
    add     sp, #8
    pop     {r4, r5, r6, r7, r8, r9, r10, pc}

@ ---- play_start: Play from stopped always sends each track's sound -----------------------------------------
@ The stock firmware only sends it on a pattern switch, so a synth switched on with the KeyStep keeps its own sound
@ until the first switch. Play sends it unconditionally, so a synth that was switched off and on on its own gets
@ its sound back too. Live here: r4 = the sequencer object; fp is the replaced load and is left as is.
play_start:
    ldrb.w  fp, [r4, #0xc]          @ the instruction the hook replaced: the play flag
    cmp.w   fp, #0
    bne     play_back               @ already running: Play pauses, nothing to send
    push    {r0, r1, r2, r3, r5, r6, r7, r8, ip, lr}
    movs    r5, #0                  @ track 0..3
play_track:
    mov     r0, r4
    mov     r1, r5
    movs    r2, #0
    bl      send_track
    adds    r5, #1
    cmp     r5, #4
    blt     play_track
    pop     {r0, r1, r2, r3, r5, r6, r7, r8, ip, lr}
play_back:
    b.w     {PLAY_BACK}

@ ---- key_press: a key sends the current track's sound when its channel's record does not know it yet --------
@ The key-press case of the panel handler. Live: everything but r2 and r3, which the replaced pair
@ (ldr r3, =KEY_LIT ; ldr r2, [r3]) writes.
key_press:
    push    {r0, r1, r4, r5, r6, r7, r8, r9, r10, r11, ip, lr}
    movw    r1, #{TRACK_LO}
    movt    r1, #{TRACK_HI}
    ldrb    r1, [r1]                @ the current track 0..3
    cmp     r1, #3
    bhi     key_done
    movw    r0, #{SEQ_LO}
    movt    r0, #{SEQ_HI}
    ldr     r0, [r0]                @ the sequencer object
    movs    r2, #1
    bl      send_track
key_done:
    pop     {r0, r1, r4, r5, r6, r7, r8, r9, r10, r11, ip, lr}
    movw    r3, #{KEY_LIT_LO}       @ the replaced pair
    movt    r3, #{KEY_LIT_HI}
    ldr     r2, [r3]
    b.w     {KEY_BACK}

@ ---- forget: the record is not trusted after a boot or a Stop ----------------------------------------------
@ The SDRAM keeps its contents through a short power-off, so a record from before a reboot would otherwise say
@ the synth (switched off and on too) still has its sound. Stop forgets as well, so the next key sends again.
boot_forget:                        @ the boot's call to the sequencer constructor comes through here
    push    {r0, r1}
    movw    r0, #{MAGIC_LO}
    movt    r0, #{MAGIC_HI}
    movs    r1, #0
    str     r1, [r0]
    pop     {r0, r1}
    b.w     {CONSTRUCTOR}
stop_forget:                        @ FUN_0803B578(seq, 0xFC): its first two instructions, plus the forget
    movs    r5, #0
    movw    ip, #{MAGIC_LO}
    movt    ip, #{MAGIC_HI}
    str     r5, [ip]
    str     r5, [sp, #4]
    b.w     {STOP_BACK}

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
