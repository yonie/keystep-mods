# Changelog

## 1.1.2 - 2026-11-08

- **`track-cc`:** each pattern remembers where you left the knobs in Control mode. When the pattern's sound is loaded
  again (a pattern switch or a Program Change) your knob values are sent right after it, so the synth keeps your
  tweaks. Save Project stores them with the project.
- **`filter-midicc`:** Play no longer resends a sound the synth already has. After Stop and Play your knob tweaks on
  the synth stay; the sound is still sent on the first Play or key after power-on.

## 1.1.1 - 2026-11-03

- **Generative:** hits and notes on swung steps play again. With swing on, every second step was silent: Pulse lost
  some of its hits (with 3 pulses over 16 steps, the third), Flux every second note.
  ([#1](https://github.com/yonie/keystep-mods/issues/1))

## 1.1 - 2026-11-01

- **New mod, `track-cc`:** Control mode follows selected track. In Control mode, knobs on the Global channel send
  their CCs on the MIDI channel of the track you entered Control mode from.
- **`filter-midicc`:** each track's pattern Program Change now also goes out when you press Play, and on the first
  key after power-on or Stop, so a synth switched off and on gets its sound back.

## 1.0 - 2026-10-27

- First release: `generative` (Flux and Pulse) and `filter-midicc` (clean pattern switches) for firmware 2.5.20, on
  the KeyStep Pro and the KeyStep Pro Chroma, and the patcher page.
