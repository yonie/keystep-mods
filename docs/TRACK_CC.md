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
| settings setter `FUN_0800E694(settings, id, value, ...)`: ids `0x26..0x2A` are stored as given, no range check | `0x0800E7F2` -> `0x0800EC42` |
| a track's MIDI Channel OUT, as the Program Change sender `FUN_0801A708` reads it | ids `0x4B..0x4E` (Tracks 1..4), `0x4F` = Track 1's Drum Output |
| Track 1's Drum mode flag, as the Program Change sender reads it | `FUN_0802B55C(seq, 0, *0x20004DAD)` |
| the selected-track byte: 0..3 a track, **4 = Control mode** | `0x200001A4` |
| the current track: unchanged by pressing Control, so in Control mode it is the track Control was entered from; selecting a track leaves Control mode | `0x20004DB7` |

## The knob Channel menu

| what | where (standard) |
|---|---|
| menu descriptors in flash, 12 bytes each: label, value-name table, number of values. The Channel one: `"Channel :"`, `0x08059F10` (`"1".."16"`, `"Global"`), 17 | `0x08058B64` (table word `0x08058B68`, count `0x08058B6C`) |
| the static constructor copies each descriptor into a menu item in RAM at boot; the Channel item is the third of the knob items (CC, Port, Channel, Min, Max at `0x20002704 + 0x1C * n`), so the count and table are read from RAM, not from the flash descriptor | `0x0801DCE4` (`add r2, r5, #0x3EC`) |
| the editor opens: stored value -> list index (`obj+0x6C`): 0..15 as is, `0x7F` -> 16, anything else -> 0 | `0x08022368`, the mapping at `0x08022396` |
| turning: the generic list editor, clamped to the item's count (it stops at the last value) | - |
| the editor commits: list index (`obj+0x6B`) -> setter: 0..15 as is, 16 -> `0x7F`, anything else is not stored | `0x08023A28`, the mapping at `0x08023A54` |
| settings save/load: the 0x9E setting bytes are written to NOR at `0xF000` behind a `0xAAAA` marker (`0x08047D48`) and read back raw at boot (`0x080462FE`); no setting is range-checked on load | - |
| MIDI Control Center sets a setting through the same setter (`0x080185E4`), no range check | - |

## The change

**Track is a new value.** The knob Channel list becomes 1..16, Global, **Track**; Track is stored as `0x7E`, a value
the stock firmware never writes there. Global keeps its stored `0x7F` and its stock meaning.

* **The list.** Two data words in the Channel descriptor: the table word points at an 18-entry table in the patch's
  own flash (`names`: the firmware's own string pointers for `"1".."16"` and `"Global"`, then the stub's `"Track"`),
  and the count goes from 17 to 18. The static constructor copies both into the menu item, so the editor turns
  through 18 values with no code change. The table and descriptor are not read anywhere else.
* **`menu_enter`** (hook `0x08022396`, `cmp r0, #0xF; bls`): `0x7E` opens the editor at index 17; everything else
  goes back to the stock mapping.
* **`menu_commit`** (hook `0x08023A54`, `cmp r2, #0xF; bls`): index 17 calls the stock setter with `0x7E`; 0..16
  go back to the stock mapping.
* **The knobs.** Both knob paths read the knob's Channel with `ldrb r4, [r3, #4]; cmp r4, #0x7F` and then `beq` to
  the Global fallback. Each of those two 4-byte pairs becomes a `b.w` to a stub (`knob_up`, `knob_down`) that calls
  `pick` and branches back to the `beq`. `pick` runs the replaced load. A channel of its own (0..15) or Global
  (`0x7F`) stays as read: stock. On Track (`0x7E`) it starts from Global, and when the selected-track byte is 4
  (Control mode) it puts the channel of the current track in `r4`: setting `0x4B + track`, or `0x4F` when that track
  is Track 1 in Drum mode. It ends with `cmp r4, #0x7F`, so the `beq` takes the stock Global fallback when the knob is
  on Global, or on Track outside Control mode.
* Only `r0`-`r3` are used (the code returned to reloads them before use); `lr` is saved around the call.
* CC number, Port, Min and Max are read by the stock code as before.

What reads the value `0x7E`: only the two knob paths (hooked) and the editor (hooked). Save and load copy the byte
raw, so Track survives a power cycle by the same path every other setting takes. MIDI Control Center would receive
`0x7E` for a knob on Track; what its editor shows for that value is not known.

Mechanics:

1. Hooks at `0x08012B18` (knob up), `0x08013FE4` (knob down), `0x08022396` (editor opens), `0x08023A54` (editor
   commits), and the data words `0x08058B68` (value names) and `0x08058B6C` (count).
2. The table, stubs and the word (240 bytes) go at `0x08060000`, after the `generative` mod's slot
   (`patches/free_space.json`).
3. No RAM: the mod keeps no state.
