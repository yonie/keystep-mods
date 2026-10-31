# The `track-cc` mod: in Control mode the knobs send on the track's channel

What the user sees is in the README. This is the map for whoever changes the patch. Firmware 2.5.20, both models;
the addresses below are the standard build's, `patches/track-cc/patch.json` has the Chroma's. Source:
`patches/track-cc/track-cc.s` (Thumb-2 assembly).

## The problem

In Control mode the five knobs send one CC each. Each knob has a Channel setting (`Utility > Controller > Knob N >
Channel`): Global (the default) or 1..16. On Global it sends on the Global MIDI channel, whatever track Control mode
was entered from:

```
Track 2 on channel 3, Control, Pitch knob:   B0 4A 01    channel 1 (Global), not the synth Track 2 plays
```

## What sends it

| what | where (standard) |
|---|---|
| the encoder handler, one panel event: knob up (`b2=7`) and knob down (`b2=8`) are two copies of the same code | `FUN_08012338` |
| knob up in Control mode: reads the knob's Channel, `0x7F` (Global) branches to load the Global MIDI channel, then builds `Bn cc val` with `FUN_08017EA6` and sends it | `0x08012B18` / fallback `0x08012DF6` |
| knob down, the same | `0x08013FE4` / fallback `0x080142C0` |
| settings getter: setting `id` is the byte at `0x20000874 + 4 + id` (`FUN_0800E666`) | knob channels ids `0x26..0x2A`, CC numbers `0x1C..0x20`, Global MIDI chan `0x50` |
| a track's MIDI Channel OUT, as the Program Change sender `FUN_0801A708` reads it | ids `0x4B..0x4E` (Tracks 1..4), `0x4F` = Track 1's Drum Output |
| Track 1's Drum mode flag, as the Program Change sender reads it | `FUN_0802B55C(seq, 0, *0x20004DAD)` |
| the selected-track byte: 0..3 a track, **4 = Control mode** | `0x200001A4` |
| the current track: unchanged by pressing Control, so in Control mode it is the track Control was entered from; selecting a track leaves Control mode | `0x20004DB7` |

## The change

Both knob paths read the knob's Channel with `ldrb r4, [r3, #4]; cmp r4, #0x7F` and then `beq` to the Global
fallback. Each of those two 4-byte pairs becomes a `b.w` to a stub (`knob_up`, `knob_down`) that calls `pick` and
branches back to the `beq`:

* **`pick`** runs the replaced pair. If the knob has its own channel, or the selected-track byte is not 4 (not
  Control mode), it leaves `r4` as read. Otherwise it puts the channel of the current track in `r4`: setting
  `0x4B + track`, or `0x4F` when that track is Track 1 in Drum mode. It ends with `cmp r4, #0x7F`, so the `beq`
  takes the stock Global fallback only when nothing was changed.
* Only `r0`-`r3` are used (the code returned to reloads them before use); `lr` is saved around the call.
* CC number, Port, Min and Max are read by the stock code as before. Nothing is drawn differently.

Mechanics:

1. Hooks at `0x08012B18` (knob up) and `0x08013FE4` (knob down), each `b.w` to its stub.
2. The stubs (124 bytes) go at `0x08060000`, after the `generative` mod's slot (`patches/free_space.json`).
3. No RAM: the mod keeps no state.
