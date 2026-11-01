@ track-cc - in Control mode, the five knobs send on the MIDI channel of the track Control mode was entered from.
@
@ KeyStep Pro main firmware 2.5.20, both models. Thumb-2. Assembled by patches/make_patch.py, which fills in the
@ {NAMES} below from patch.json (the firmware addresses differ per model). How it hooks the firmware:
@ docs/TRACK_CC.md. No RAM: the patch keeps no state.
@
@ Hooks (4-byte instructions, overwritten by the patcher), in the encoder handler FUN_08012338 (standard):
@   0x08012B18  ldrb r4,[r3,#4] ; cmp r4,#0x7F  ->  b.w knob_up     (a knob turned up in Control mode)
@   0x08013FE4  ldrb r4,[r3,#4] ; cmp r4,#0x7F  ->  b.w knob_down   (a knob turned down in Control mode)
@ Both places read the knob's Channel setting (Utility > Ctrl > Knob N: 0x7F = Global, else 0..15) into r4 and
@ branch on 0x7F to load the Global MIDI channel instead. The stubs run that same instruction pair, put the track's
@ channel in r4 when the knob is on Global and Control mode is on, and return to the conditional branch with the
@ flags of `cmp r4, #0x7F`: equal only when the stock Global fallback should still run.
@ Live at the hook: r3 = settings + this knob's index + 0x26, r4 = result. r0-r3 are free (the code returned to
@ reloads them before use); lr is kept.

knob_up:
    push    {r1, lr}
    bl      pick
    pop     {r1, lr}                @ pop leaves the flags alone
    b.w     {UP_RESUME}             @ the stock `beq.w` to the Global fallback

knob_down:
    push    {r1, lr}
    bl      pick
    pop     {r1, lr}
    b.w     {DOWN_RESUME}

@ ---- pick: r4 = the channel this knob sends on; flags = cmp r4, #0x7F -----------------------------------------
pick:
    ldrb    r4, [r3, #4]            @ the replaced instruction: the knob's Channel setting
    cmp     r4, #0x7F
    bne     keep                    @ the knob has its own channel: stock
    movw    r0, #{MODE_LO}
    movt    r0, #{MODE_HI}
    ldrb    r0, [r0]
    cmp     r0, #4                  @ 4 = Control mode (the selected-track byte; 0..3 = a track)
    bne     keep
    movw    r1, #{TRACK_LO}
    movt    r1, #{TRACK_HI}
    ldrb    r1, [r1]                @ the track Control mode was entered from (its button blinks), 0..3
    cmp     r1, #3
    bhi     keep
    adds    r2, r1, #0x4B           @ settings id of that track's MIDI Channel OUT (0x4B..0x4E)
    cbnz    r1, chan
    push    {r2, lr}                @ Track 1: Drum mode sends on its Drum Output channel
    movw    r0, #{SEQ_LO}
    movt    r0, #{SEQ_HI}
    ldr     r0, [r0]                @ the sequencer object
    movw    r2, #{PROJECT_LO}
    movt    r2, #{PROJECT_HI}
    ldrb    r2, [r2]
    movs    r1, #0
    bl      {DRUM}                  @ Track 1's Drum mode flag, as the Program Change sender reads it
    pop     {r2, lr}
    cbz     r0, chan
    movs    r2, #0x4F               @ settings id of the Drum Output channel
chan:
    movw    r0, #{SETTINGS_LO}
    movt    r0, #{SETTINGS_HI}
    adds    r0, r0, r2
    ldrb    r4, [r0, #4]            @ the firmware's settings getter: *(settings + 4 + id)
    and     r4, r4, #15
keep:
    cmp     r4, #0x7F
    bx      lr
