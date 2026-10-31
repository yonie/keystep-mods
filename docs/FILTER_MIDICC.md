# The `filter-midicc` mod: no repeated program change on a pattern switch

What the user sees is in the README. This is the map for whoever changes the patch. Firmware 2.5.20, both models;
the addresses below are the standard build's, `patches/filter-midicc/patch.json` has the Chroma's. Source:
`patches/filter-midicc/filter-midicc.s` (Thumb-2 assembly).

## The problem

A pattern can carry a Program Change with its Bank MSB and LSB (`Shift + Track > PC Assign`). On every pattern
switch the KeyStep Pro sends all three, even when the new pattern's values are the same as the old one's:

```
every switch:           B0 00 04 | B0 20 00 | C0 02 | 90 3C 64     all in the same millisecond, on the downbeat
a pattern that loops:   90 3C 64                                   no program change
```

Many synths (a Roland JV-1010, for one) reload the sound on every program change, even an identical one, so the
first note after a switch lands on a sound load and plays late.

## What sends it

| function | role |
|---|---|
| `FUN_0801A708(port, msb, lsb, pc, track)` | the sender. Looks up the track's MIDI channel (Track 1 picks its drum or synth channel), then, if PC is not OFF and MSB/LSB < 0x80, always sends `Bn 00 msb`, `Bn 20 lsb`, `Cn pc-1`. No memory of what it sent before |
| `FUN_08035404` (call at `0x08035618`) | loads a pattern on a track; when the pattern really changes it reads that pattern's PC/MSB/LSB and calls the sender |
| `FUN_0803A614` | applies a pattern switch at the bar line and calls `FUN_08035404` |
| `FUN_08022C68` / `FUN_08022CDC` / `FUN_08022D50` | the PC Val / MSB / LSB editors: call the sender on every encoder detent (the live preview) |

## The change

One record per MIDI channel of what that channel last received: 16 x (MSB, LSB, PC, marker) = 64 bytes. A
channel counts as known only when its marker byte is `0xA5`, so whatever the RAM holds at power-up, the first
send on each channel always goes out.

* **Pattern switch:** if MSB, LSB and PC all equal the channel's record, nothing is sent. Any difference sends
  all three (a bank select only takes effect at the next program change), then the record is updated.
* **Menu editors:** unchanged: they send on every detent, and they update the record, so it always says what the
  synth has.
* **Per channel, not per track:** a synth holds one sound per channel. Tracks sharing a channel and a track
  changing channel mid-play both stay correct.

Mechanics:

1. The call at `0x08035618` becomes a call to the stub `switch_send`, which sets a flag to `0xA5`, re-pushes the
   5th (stack) argument, calls the original `FUN_0801A708`, and clears the flag.
2. Inside `FUN_0801A708`, right after the channel is known and the range checks pass, the instruction at
   `0x0801A7E8` becomes a branch to the stub `check_and_record`: if the flag is set and the record matches, it
   returns from `FUN_0801A708`; otherwise it updates the record and continues to the three sends.
3. The stubs (286 bytes, with `play_start` below) go right after the stock image, at `0x0805D1AC`. The 69 bytes of RAM (marker + record + flag) go
   in SDRAM at `0xC01ECF00`, a block the stock firmware never uses (`patches/free_space.json`). It is not cleared
   at boot: the first call after power-on finds no marker, clears the record and sets it.
4. The menu editors are not touched.

## Play sends the current sound

The stock firmware sends a pattern's PC only on a pattern switch or a menu edit, never on Play. After power-on
the synth therefore stays on its own sound until the first switch.

| what | standard | Chroma |
|---|---|---|
| `FUN_0803B578(seq, code)`, the transport handler; `0xFA` (Play, panel or MIDI Start) branches to | `0x0803BCAE` | `0x0803C22E` |
| hook: `ldrb.w fp, [r4, #0xc]` (the play flag; 0 = start from stopped or paused) | `0x0803BCB6` | `0x0803C236` |
| each track's current pattern, one byte per track (written by `FUN_08030904`) | `0x20004DA8` | `0x20004DC8` |
| the current bank, the 4th argument of the getters | `0x20004DAD` | `0x20004DCD` |
| `*port`, the sender's 1st argument | `*0x20001800` | `*0x20001804` |
| MSB / LSB / PC getters `(seq, track, pattern, bank)` | `0x0802F112` / `0x0802F14C` / `0x0802F0D8` | `0x0802F884` / `0x0802F8B8` / `0x0802F850` |

The hook branches to `play_start`, which runs the replaced load and, when the play flag is 0, does for tracks
0..3 exactly what the pattern switch does in `FUN_08035404` (`0x080355E0..0x08035618`): the three getters on the
track's current pattern, then the sender through `switch_send`. So the record decides: the first Play after
power-on sends each track's sound, a later Play sends nothing unless something changed, and a track with PC Send
off or PC OFF sends nothing, as on a switch. It goes out before the first note, in the same millisecond.
