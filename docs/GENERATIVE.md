# The `generative` mod: how it hooks the firmware

What the user sees is in the README ("Generative mode": Flux, a Turing machine, and Pulse, Euclidean rhythms). This
is the map for whoever changes the patch: the hooks, the firmware addresses they rely on, and how a generated note or
drum hit gets played without touching the pattern.
Firmware 2.5.20 only, both models: the standard KeyStep Pro (main build 2085) and the KeyStep Pro Chroma (main
build 375); their UI images are the same code. The addresses below are the standard build's; `patches/generative/fw.h`
holds both columns, `patch.json` the hook sites and places per model. On the Chroma the sequencer object's
per-project record is 0x1000 bytes instead of 0xFEC (fw.h `PROJ_STRIDE`); every offset the mod uses inside it is the
same. Sources: `patches/generative/generative.c` (the logic, C), `patches/generative/hooks.S` (the main-MCU shims),
`patches/generative/ui_letters.S` (the UI MCU's letters), `patches/generative/patch.json` (where they go).

## Where it lives

| | range | |
|---|---|---|
| main flash | `0x0805D400..0x08060000` (Chroma: `0x0805DC00..`) | after the stock image (ends `0x0805D1AC`) and `filter-midicc`'s slot; about 4.4 KB used |
| main RAM | `0xC01EC000..0xC01ECF00` (SDRAM) | a block of the 2 MB SDRAM the firmware never uses; about 0x700 bytes used, 1 KB of it the Pulse save area |
| UI flash | `0x0802F010..0x0802F100` | after the stock UI image (ends `0x0802F004`); 52 bytes used, no RAM |

That SDRAM is not cleared at boot, so the state carries a marker (`"GEN1"`) and is zeroed the first time any hook
runs without it. (Not the CCM at `0x10000000`: the main MCU has no working CCM, and a first build that kept its
state there left the unit on its boot logo.) `make_patch.py` builds the C with arm-none-eabi-gcc (`-Os`, no libc, `.data` must be empty)
and links it at those addresses.

## The hooks

| at | replaces | hook | what for |
|---|---|---|---|
| `0x080500F4` | `bl FUN_0804AC6E` in the press handler `FUN_080500D8` | `gen_press_hook` | the Seq + Arp/Drum chord, and leaving with Seq or Arp/Drum alone |
| `0x08039BDC` | `push.w {r4-r11,lr}`, entry of the step player `FUN_08039BDC(seq, track)` | `gen_play_hook` | a generative track plays its generated note |
| `0x08012730` | `sub.w r8, r6, #3` (turn up) in `FUN_08012338` | `gen_turn_up_hook` | the five encoders set the generator |
| `0x08013BE6` | `sub.w r8, r6, #3` (turn down) | `gen_turn_down_hook` | same |
| `0x0801269C` | `bl FUN_080100BC` (stock encoder popup) | `gen_touch_hook` | our popup on touch |
| `0x080125AC` | `bl FUN_0801F7D8` (stock popup timer) | `gen_untouch_hook` | our popup's release |
| `0x0801696A` | `bl FUN_08054C2C` (main loop LED flush) | `gen_flush_hook` | the step-LED and popup tick runs after the stock flush |
| `0x080102E0` | `push {r4,lr}; sub sp,#0x10`, entry of `FUN_080102E0(port, buf)` | `gen_tx_hook` | every packet to the UI MCU: drop the stock step colours, send the letters for the track display |
| UI `0x0800B1D4` | `movs r3,r2; subs r3,#0x64` in the UI's display writer `FUN_0800B1C4` | `ui_letters` | values `0xA0`/`0xA1` draw "FL"/"PU" |

Every hook that is not about a generative track falls straight through to the stock code with all registers as
they were. With no track generative, the patched firmware behaves as stock: the test cases that do not enter the
mode compare against stock.

### The chord (press handler)

`FUN_080500D8(panel, code, 1)` handles a button going down. At `0x080500F4` `r6` = code, `r4` = panel. The hook
first makes the replaced call, then `gen_on_press(code, panel)`:

- Track t's buttons: Seq = `0x14 + 6(t-1)`, Arp/Drum = Seq + 1. The partner is `code ^ 1`.
- A button is held while byte 2 of its record `panel + 0x6C + code*3` is 0.
- Partner held → enter generative on that track and leave the stock handler through its epilogue `0x0805031E`, so
  the stock mode switch never happens. Partner not held and the track is generative → leave generative, and let
  the stock handler switch the mode as usual.
- Ignored while buttons are sent as MIDI (`0x20002D58`) and, for Track 1, while recording is armed.
- While the selected track is generative (and no menu is open), Last Step (`0x3E`) and the page buttons
  (`0x3F..0x42`) are taken here: the stock Last Step writes the stored pattern's length (`seq + 0x17C`,
  `seq + 0x19F` and its pattern record), so Last Step + Step N (`0x2E..0x3D`) sets Loop Length / the lane's Steps
  instead, Last Step alone shows the popup, and the pages do nothing (a generative loop is 16 steps at most).
- A step button (`0x2E..0x3D`) on its own turns that Flux step on or off and is taken here too (in Pulse it does
  nothing); its held flag is then cleared, because the key handler step-enters a note into the stored pattern
  for any held step.
- Record (`0x02`) is left to the stock handler, which arms and disarms recording: see below.

### Record: writing what plays into the pattern

While the stock Record is armed (`REC_ARMED`, `0x20002D5B` / `0x20002D7B`, toggled by the stock Record press,
playing or not), the step player marks every step a generative track plays: Flux its loop step with the note
heard (or a rest) and velocity, Pulse every lane's step with whether it hit. The mark is a byte flag per step,
written after the data; the main loop (`gen_tick`, and `leave` before the track leaves) clears the flag and then
reads the data, so nothing needs interrupts off. Each marked step is then rewritten with the firmware's own step editing: its old
notes (Pulse: that lane's) are removed, the on bit cleared, and what was heard added:

| firmware (standard / Chroma) | what it does |
|---|---|
| `FUN_0802F604` / `0x0802FD58` `(seq, slot, step, t, pat, proj, 0)` | removes a note |
| `FUN_0802F404` / `0x0802FB58` `(seq, note, slot, step, t, pat, proj, 1)` | adds a note: allocates a pool entry, fills it from the pattern's template, sets the slot |
| `FUN_0802CB30` / `0x0802D1C8` `(seq, step, t)` | the step's first free slot (drum rows hold several lanes) |
| `FUN_08032A98` / `0x0803311C` `(seq, 1, step, pat, t, proj, lane, 0)` | the step's on bit (Seq: per track, Drum: per lane) |
| `FUN_0802B1A8` / `0x0802B71C` `(seq, last, t, lane)` | the playing length; lane `0x18` = the whole track |

After each add the pool entry is overwritten with our values (gate code 7, velocity, offset `0x31`, probability
100). The stored length is written in the pattern record too, the way
`FUN_0802EAEA` reads it: Seq `seq + proj*STRIDE + t*0x140 + pat*0x14 + 0x938` = last step; Drum
`seq + proj*STRIDE + pat*0x20 + 0xE37 + lane` per lane, with bit 2 of `+0xE4F` set so the lanes keep their own
lengths. The written step's ratchet mask is set to `0xF` (none). When a track that recorded leaves generative mode, the
playhead is set to the step being played (`seq + 0x180 + t`, Drum lanes `seq + 0x1D3 + lane`), so the pattern
plays on from where the loop was.

Entering Tracks 2..4 forces the track into Seq mode (`FUN_0802B8D0(seq, 0, t, bank)` and the LEDs with
`FUN_0804CA80`), so the step player runs its sequencer path; Track 1 is forced into Drum mode (mode 1), so it runs
the drum path. Both mode LEDs are then kept lit.

### Playing a note: Flux (step player)

The step player `FUN_08039BDC(seq, t)` runs from the TIM2 compare interrupt, once per step per track. For a
generative track the hook calls `gen_play(seq, t)`, which for a Flux track:

1. advances the generator's own position (modulo Loop Length, in the pattern's direction, below) and, with
   probability Change, rolls a new note for that position;
2. takes the stock step the player is on, `seq[0x180 + t]`, and saves everything the player reads for it:
   - the track's whole slot table, 64 rows of 16 (`0x20003DA0 + t*0x400`; this step's row is `+ step*0x10`), which
     it blanks: the player's look-ahead also plays the next step's notes when they are nudged ahead of it with Time
     Shift, so blanking only this step's row let a stored pattern play through (found on the device, 2026-10-27);
   - pool entry 0 of the pattern: `0xC0000000 + proj*0xF000 + t*0x3C00 + pat*0x3C0` (5 bytes: note, gate code,
     velocity, micro-offset, probability);
   - its step-on bit: `seq + proj*0xFEC + t*0x140 + pat*0x14 + 0x93C` (64-bit mask);
   - its 4-bit step mask: `0xC0104000 + (pat + (t + proj*5)*0x10)*0x20`;
   - its tie flag: `0x20002D5C + t*0xC0 + idx`;
3. writes our note there (note = `60 - Range/2 + n`, gate code 7 = 50 %, velocity 84..123, no offset, probability
   100), or clears the step for a rest;
4. calls the stock player (`gen_play_orig`, the replaced `push.w` followed by a branch to `0x08039BE0`);
5. puts every saved byte back.

So the note goes out through the firmware's own path (channel, gate length and note-off, USB, DIN, CV/gate,
mute, swing, and the track's scale and root, which quantise it) and the pattern in memory is the same before and
after, byte for byte.

Project = `*(u8*)0x20004DAD`, pattern of track t = `*(u8*)(0x20004DA8 + t)`.

### Playing drum hits: Pulse (Track 1 in Drum mode)

There is no separate drum player: `FUN_08039BDC` itself, when `FUN_0802CCA2(seq, t, P)` says Track 1 is in Drum
mode, walks the 24 lanes. For each lane it takes the lane's playhead `s` (`seq + 0x1D3 + lane`, or
`seq + 0x1EB + lane` when the pattern plays Rand/Walk), reads the 16 slots of row `s` of Track 1's slot table
(`0x20003DA0 + s*0x10`) and plays each pool entry there whose lane maps to this lane's note, when the lane's
step-on bit, the entry's tie flag, probability and ratchet nibble allow it. Drum memory (Track 1, project P,
pattern pat):

- pool entries: `0xC0114000 + P*0x3C00 + pat*0x3C0 + idx*5`, bytes {lane, gate code, velocity, micro-offset,
  probability};
- step-on bits per lane: a u64 at `0xC0150000 + (lane + pat*0x18 + P*0x180)*8`;
- ratchet nibble per entry: `0xC010E000 + P*0x600 + pat*0x60 + idx/2` (odd idx: high nibble);
- tie flag per entry: `0x20002D5C + idx`;
- the pattern record: `seq + P*0xFEC + pat*0x20 + 0xE36`; `+0x19` bits 6..7 are the direction (0 Fwd, 1 Rand,
  2 Walk).

The selected lane (set by pressing a key in Drum mode) is `*(u8*)0x20003D9D`. Lane l plays note 36 + l by default
(`FUN_080347C8`; a custom map at `0x20000874 + 0x57`), on the drum channel (`seq + P*0xFEC + 0x17CF`, default 10).
Lanes 0..7 also drive drum gates 1..8.

For one step `pulse_play` advances each lane's own position (modulo its Steps), decides its hit
(`((i - Rotate) * Pulses) mod Steps < Pulses`, then Chance), saves Track 1's whole slot table (64 rows, 1 KB, into
its own RAM) and blanks it, and for the j-th lane that plays writes pool entry j {lane, 7, 100, 0x31, 100}, sets its
ratchet nibble, clears its tie flag, sets the lane's step-on bit for its row and puts j in that row. Then the stock
player runs, and every byte is put back. Blanking the whole table also keeps stored hits out (the player's
look-ahead reads the next row too).

### Settings it follows

- **Playback direction** (Shift + Fwd / Rand / Walk): Seq mode keeps it per pattern in bits 3..4 of
  `seq + P*0xFEC + t*0x140 + pat*0x14 + 0x939`; Drum mode in bits 6..7 of the drum record's `+0x19`. The
  generators move their own positions the same way. Walk's odds were measured on the stock firmware: next step
  49 %, previous 24 %, the same 27 %, so 50/25/25.
- **Scale, root, time division, triplets**: nothing to do; the stock player quantises the note and clocks the
  steps.
- **Play and Shift + Play**: `seq[0x0C]` is 1 while the transport runs. `gen_tick` clears a flag while it is 0, and
  `gen_on_press` clears it on Shift + Play; the next step then restarts every loop from its first step.

### Encoders

`FUN_08012338` dispatches encoder events. At both turn sites `r6` = the knob code (3..7 = Pitch, Gate, Velocity,
Time Shift, Randomness) and the magnitude is at `[sp+0x6b]`. The hook computes the stock `r8 = r6 - 3` and, when
`gen_turn` took the turn (selected track generative, no menu open: `M + 0x0E` is 0), sets `r8 = 0xFF`; the stock
`cmp r8, #4; bhi` then skips to the next event. On Track 1 the knobs set the selected lane (Pitch moves
`0x20003D9D` itself).

Touch and release are replaced calls: when ours, the popup is drawn as a timed OLED page
(`FUN_08025160(D, fn, 0, 1, 0x32, ms)` with D = `*0x200017F8`) using the firmware's own clear, title row, big
row and commit (`FUN_08026326`, `FUN_0802648A`, `FUN_080264CE`, `FUN_080263AA`); otherwise the stock function runs.

### LEDs

The step LEDs are RGB, driven by the main MCU as type-2 group-3 packets to the UI MCU. `gen_tick` (after the
stock LED flush) builds its own with `FUN_0800F6D8(buf, 2, 3, step, &w)` and sends them with `FUN_080102E0`, only
when a colour changed. `gen_tx_hook` drops the stock group-3 packets while the selected track is generative. When
the selected track changes, or the track leaves the mode, the stock step LEDs are marked dirty and the stock flush
redraws them. Seq and Arp/Drum are kept lit with `FUN_08053EB8(leds, elem, 1, colour, 0, 0)`.

### The track display: letters, in the UI firmware

The main MCU sets a track display with a group-1 packet `AA 12 01 track v0 v1 00 00 chk` (v1: bit 0 the ones dot,
bit 1 the tens dot). The UI MCU (a Cortex-M0) draws it in `FUN_0800B1C4(0x200014D0, track, v0, v1)`: 0..99 from the
digit table at `0x08027934`, `0xFF` blank, 100..254 ignored. Its shadow of the TM1638 display RAM is at
`0x200014DE` (T1 tens/ones = bytes 8/10, T2 9/11, T3 12/14, T4 13/15) and `FUN_0800ADA8` sends it over SPI1.
Nothing in the stock firmware draws anything but digits there (the host test commands give 00..99, blank or 8.8.).

`ui_letters` replaces the two instructions at `0x0800B1D4`: for `v0` = `0xA0 + n` it loads pair n from its own
table into r3/r7, adds the dots and returns to the stock write at `0x0800B218`; anything else runs the replaced
instructions. `gen_tx_filter` sends `0xA0` ("FL") for a Flux track and `0xA1` ("PU") for Pulse. Segment bits:
0 a (top), 1 b, 2 c, 3 d, 4 e, 5 f, 6 g (middle), 7 the dot, as the firmware's own digit table has them.
