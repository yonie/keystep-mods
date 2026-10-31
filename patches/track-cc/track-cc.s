@ track-cc - in Control mode, a knob whose Channel is set to Track sends on the MIDI channel of the track Control
@ mode was entered from.
@
@ KeyStep Pro main firmware 2.5.20, both models. Thumb-2. Assembled by patches/make_patch.py, which fills in the
@ {NAMES} below from patch.json (the firmware addresses differ per model). How it hooks the firmware:
@ docs/TRACK_CC.md. No RAM: the patch keeps no state.
@
@ The knob Channel setting (Utility > Controller > Knob N > Channel, settings ids 0x26..0x2A) stores 0..15 for a
@ channel and 0x7F for Global. This patch adds a third kind of value, 0x7E = Track, as an 18th entry of the
@ setting's list (1..16, Global, Track). Hooks (4-byte instructions, overwritten by the patcher), standard build:
@   0x08022396  cmp r0,#0xF ; bls        ->  b.w menu_enter   (the editor opens: stored value -> list index)
@   0x08023A54  cmp r2,#0xF ; bls        ->  b.w menu_commit  (the editor commits: list index -> stored value)
@   0x08012B18  ldrb r4,[r3,#4] ; cmp r4,#0x7F  ->  b.w knob_up     (a knob turned up in Control mode)
@   0x08013FE4  ldrb r4,[r3,#4] ; cmp r4,#0x7F  ->  b.w knob_down   (a knob turned down in Control mode)
@ and two data words in the setting's menu descriptor, which the firmware copies into the menu item at boot:
@ its value-name table points at `names` below, and its count is 18.

@ ---- the knob Channel setting's value names: the firmware's own strings for 1..16 and Global, then "Track" ------
@ (pointers to the firmware's strings; "1".."16" are 16 bytes apart in both builds)
names:
    .word   {NAME_1}
    .word   {NAME_1}+0x10
    .word   {NAME_1}+0x20
    .word   {NAME_1}+0x30
    .word   {NAME_1}+0x40
    .word   {NAME_1}+0x50
    .word   {NAME_1}+0x60
    .word   {NAME_1}+0x70
    .word   {NAME_1}+0x80
    .word   {NAME_1}+0x90
    .word   {NAME_1}+0xA0
    .word   {NAME_1}+0xB0
    .word   {NAME_1}+0xC0
    .word   {NAME_1}+0xD0
    .word   {NAME_1}+0xE0
    .word   {NAME_1}+0xF0
    .word   {NAME_GLOBAL}
    .word   name_track

@ ---- menu_enter: r0 = the knob's stored Channel; stock maps 0..15 -> index, 0x7F -> 16, anything else -> 0 --------
menu_enter:
    cmp     r0, #0xF
    bls     enter_index
    cmp     r0, #0x7E
    bne     enter_stock
    movs    r0, #17                 @ Track is the 18th entry
enter_index:
    b.w     {ENTER_INDEX}           @ strb r0, [r4, #0x6c]
enter_stock:
    b.w     {ENTER_REST}            @ the stock `cmp r0, #0x7F` (Global -> 16, else 0)

@ ---- menu_commit: r2 = the list index picked; stock stores 0..15 as is, 16 as 0x7F, ignores anything else -------
menu_commit:
    cmp     r2, #0xF
    bls     commit_store
    cmp     r2, #17
    bne     commit_stock
    movs    r2, #0x7E               @ Track
commit_store:
    b.w     {COMMIT_STORE}          @ the settings setter with r2 as the value
commit_stock:
    b.w     {COMMIT_REST}           @ the stock `cmp r2, #0x10` (Global -> 0x7F, else nothing)

@ ---- the knobs in Control mode ---------------------------------------------------------------------------------
@ Both knob paths read the knob's Channel into r4 and branch on 0x7F to load the Global MIDI channel instead. The
@ stubs run that same instruction pair through `pick` and return to the conditional branch with the flags of
@ `cmp r4, #0x7F`: equal only when the stock Global fallback should run. Live at the hook: r3 = settings + this
@ knob's index + 0x26, r4 = result. r0-r3 are free (the code returned to reloads them before use); lr is kept.
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
    cmp     r4, #0x7E
    bne     keep                    @ a channel of its own, or Global: stock
    movs    r4, #0x7F               @ Track: Global unless the track's channel is found below
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

name_track:
    .byte   0x54, 0x72, 0x61, 0x63, 0x6B, 0     @ "Track"
