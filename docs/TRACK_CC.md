# The `track-cc` mod: in Control mode the knobs follow the track, and each pattern keeps their values

What the user sees is in the README. This is the map for whoever changes the patch. Firmware 2.5.20, both models;
the addresses below are the standard build's, `patches/track-cc/fw.h` and `patch.json` have the Chroma's. Source:
`patches/track-cc/track-cc.c` (C) and `patches/track-cc/hooks.S` (the hook shims).

## The problem

In Control mode the five knobs send one CC each. Each knob has a Channel setting (`Utility > Controller > Knob N >
Channel`): Global (the default) or 1..16. On Global it sends on the Global MIDI channel, whatever track Control mode
was entered from:

```
Track 2 on channel 3, Control, Pitch knob:   B0 4A 01    channel 1 (Global), not the synth Track 2 plays
```

And a Program Change makes most synths reload the stored patch, so every pattern switch (or any other Program Change)
throws away whatever the knobs did to the sound.

## What sends it

| what | where (standard) |
|---|---|
| the encoder handler, one panel event: knob up (`b2=7`) and knob down (`b2=8`) are two copies of the same code | `FUN_08012338` |
| knob up in Control mode: reads the knob's Channel, `0x7F` (Global) branches to load the Global MIDI channel, then builds `Bn cc val` with `FUN_08017EA6(ch, cc, val)` and sends it with `FUN_0801A6E0(*0x20001800, msg, port)`. At the read, `r8` is the knob 0..4 and `r7` the value it sends (after Min/Max) | `0x08012B18` / fallback `0x08012DF6` |
| knob down, the same | `0x08013FE4` / fallback `0x080142C0` |
| settings getter: setting `id` is the byte at `0x20000874 + 4 + id` (`FUN_0800E666`) | knob CC numbers `0x1C..0x20`, ports `0x21..0x25`, channels `0x26..0x2A`, Global MIDI chan `0x50` |
| a track's MIDI Channel OUT | ids `0x4B..0x4E` (Tracks 1..4), `0x4F` = Track 1's Drum Output |
| Track 1's Drum mode flag | `FUN_0802B55C(seq, 0, *0x20004DAD)` |
| the selected-track byte: 0..3 a track, **4 = Control mode** | `0x200001A4` |
| the current track: unchanged by pressing Control, so in Control mode it is the track Control was entered from | `0x20004DB7` |
| the current project; each track's current pattern | `0x20004DAD`; `0x20004DA8 + track` |
| the Program Change sender `FUN_0801A708(ports, msb, lsb, pc, track)`: when PC Send is on and PC is set, `Bn 00 msb`, `Bn 20 lsb`, `Cn pc`; the PC is its last send, `bl FUN_0801A6E0` | `0x0801A86A` |
| the pattern switch `FUN_08035404(seq, pattern, track, x, send)`: with `send` set (every pattern switch and project change; not the reload of the same pattern) it runs the PC block `0x080355E0..0x0803561C` (the getters, then the sender), then both paths meet at `0x08035562` | `0x0803555C` / `0x08035562` |
| erase + program a 64 KiB NOR sector from the staging buffer `0xC01F0000` (`r1` = the sector) | `FUN_080475D8` |
| load a project from NOR into SDRAM `(h, src, dst)`: 16 times at boot, after Copy Project, Erase Project and a MIDI Control Center commit | `FUN_08046858` |
| the NOR read primitive `(nor, src, dst, halfwords)`, the NOR handle | `FUN_0803F774`, `0x20001820` |
| the project save `(h, src, dst)`; Copy Project calls it with the current project as `src` and the target as `dst` | `FUN_08047C84`, call `0x0802418C` |
| the settings-store save and the 0xFFF0 block save, the two calls of the program function that are not project data | `0x08047D88`, `0x08047DC0` |

## The change

**The channel.** Both knob paths read the knob's Channel with `ldrb r4, [r3, #4]; cmp r4, #0x7F` and then `beq` to
the Global fallback. Each pair becomes a `b.w` to a shim (`knob_up`, `knob_down`) that calls `tcc_knob(setting, knob,
value)` and branches back to the `beq` with the flags of `cmp r4, #0x7F`. If the knob has its own channel, or the
selected-track byte is not 4, `r4` is left as read. Otherwise `r4` becomes the current track's channel: setting
`0x4B + track`, or `0x4F` when that is Track 1 in Drum mode. CC number, Port, Min and Max are read by the stock code as
before.

**Keeping the values.** In that same call (a Global knob, Control mode, a track 0..3) the value the knob sends (`r7`) is
kept under (current project, track, that track's current pattern, knob). Knobs with their own channel keep nothing.

**Sending them.** `send_kept(track, pattern)` sends the kept values of that pattern of the current project, knob 1..5,
each only if a value is kept and the knob is still on Global: `FUN_0801A6E0(*0x20001800, FUN_08017EA6(channel, CC
number, value), port)`, the knob's own CC number and port(s), on the track's channel (as above). It runs:

* **after a Program Change**: the sender's PC send (`0x0801A86A`) becomes `bl pc_tail`, which sends the PC and then
  the kept values of the track's current pattern. That covers every path that sends a PC through the sender: the menu
  editors, and with the `filter-midicc` mod its Play and key sends. A PC that `filter-midicc` skips never gets here.
* **after a pattern switch, PC or not**: `cmp.w fp, #0` at `0x0803555C` (the test of the switch's send flag) becomes a
  `b.w sw_check`, which notes that a switch is under way before the PC block runs; while that note is set `pc_tail`
  sends nothing. The meeting point `0x08035562` becomes a `b.w sw_merge`: it clears the note and sends the kept values
  of the pattern just loaded, after the PC went out, was skipped, or with PC Send off, and before the pattern's first
  note (the switch runs before the sequencer plays the bar). A project change loads every track's pattern this way.

**Saving them.** Per project 320 bytes (4 tracks x 16 patterns x 5 knobs, `0xFF` = nothing kept). In NOR they sit at
`0xE000` in the project's sector A, a range the firmware never fills and reads back and programs again unchanged on
every save of that sector:

```
0x000  "TCCK"           magic
0x004  u16  1           version
0x006  u16  320         length
0x008  320 bytes        (track * 16 + pattern) * 5 + knob
0x148  u32              CRC-32 (IEEE) of 0x000..0x147
```

* **Save:** the entry of `FUN_080475D8` (`push {r3-r7, lr} ; mov r4, r1`) becomes a `b.w save_hook`. When `r1` is a
  project's sector A and the call is not the settings-store or 0xFFF0 save (told apart by the return address), the
  block of that project is written into the staging buffer at `0xE000` before the firmware erases and programs the
  sector. So Save Project (and Save Pattern / Save Track, which program sector A too) save the values; nothing else
  does, and no erase or program of our own is ever added. A settings change or the store save at every boot keep what
  NOR already has.
* **Load:** the entry of `FUN_08046858` (`push.w {r4-r8, lr}`; Chroma `push.w {r4-r11, lr}`) becomes a `b.w
  load_hook`, which runs the stock load as a subroutine and then reads the block of project `src` into project `dst`'s
  RAM with the firmware's NOR read primitive, in the same context as the firmware's own read just before it. The
  header is read first; the rest only when it is ours. A bad magic, version, length or CRC (a fresh unit, an erased
  project, damage) loads as nothing kept. So the boot loads all 16 projects before anything can be sent or saved; Erase
  Project and a MIDI Control Center commit reload from NOR (unsaved values of that project are dropped).
* **Copy Project:** the call `bl FUN_08047C84` at `0x0802418C` becomes `bl copy_hook`, which copies the source's
  values to the target's RAM first; the target's save then writes them into the target's block.

**RAM.** `0xC01ED000..0xC01EE590` in the SDRAM block no firmware code uses (`patches/free_space.json`): the values of
the 16 projects, a "loaded" marker per project, a read buffer and the switch note. It is not cleared at boot: a
project's values are used (recorded, sent or saved) only once the load has marked it.

Mechanics:

1. Eight hooks: knob up `0x08012B18`, knob down `0x08013FE4`, the sender's PC `0x0801A86A`, the switch's send-flag test
   `0x0803555C` and meeting point `0x08035562`, the program entry `0x080475D8`, the load entry `0x08046858`, Copy
   Project's save `0x0802418C`. None is shared with another mod; each works without the others.
2. The code (about 1.2 KB) goes at `0x08060000` (Chroma `0x08060800`), after the `generative` mod's slot.
3. Nothing is drawn differently. Outside Control mode the knobs are stock and nothing is kept.

## Limits

* The values follow the current pattern of the track at the moment the PC goes out: a PC Val edited in the menu for
  another pattern than the current one is followed by the current pattern's values.
* A kept value goes out on the knob's current CC number and port; a knob moved to a fixed channel since is skipped.
* Only Save Project / Pattern / Track store the values; switching the unit off without saving loses them, like the
  firmware's own unsaved edits.
