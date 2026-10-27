# keystep-mods - agent instructions

Mods for the Arturia KeyStep Pro firmware 2.5.20 (standard and Chroma), and the patcher that applies them to the
user's own stock update file. The global instructions in `G:\code\AGENTS.md` and `C:\Users\yonie\AGENTS.md` apply.

## This repo is published as it is

- **Only our own bytes.** Never commit a firmware image, stock or patched (`*.kspf`, `*.bin` are ignored), nor
  any byte copied out of Arturia's firmware. A mod is its source, its manifest and the addresses it hooks.
- **Nothing about how mods are tested lives here.** No test suites, test commands, tool names or results: they are
  kept outside this repo, and every mod is proven there against stock, on both models, before its bytes are
  exported. Do not add references to that tooling in code, comments or docs.
- **No picture of the KeyStep, and no screen captures** (Ronald, 2026-10-27: stay clear of
  Arturia's ownership of the instrument's look). The README links to the patcher page (GitHub Pages, published
  from `web/` by `.github/workflows/pages.yml`) instead of showing it; the page links back to the README. Anything else is a stylised drawing made by `docs/oled.py`: the OLED popups from their text, the step LEDs as
  an abstract row of squares. Never draw the device's shape or panel layout.
- **No vendor names as mod names.** Mods get their own names, never Arturia feature names (Mutate, Spice, Dice...).
- After changing a mod: `python patches/make_patch.py --export`, then `node web/test_kspatch.js` (the web
  patcher must stay byte-identical to `make_patch.py`).

## Layout

- `patches/make_patch.py` - stock `.kspf` + mod names -> patched `.kspf`; `--export` writes `web/patches/`.
- `patches/free_space.json` - the flash and RAM measured free in each stock image, and each mod's slot per model.
- `patches/<mod>/patch.json` - per model: the image, the place, the firmware symbols, the hook sites.
- `patches/generative/fw.h` - the firmware addresses generative's C and assembly use, one column per model.
- `docs/` - how each mod hooks the firmware.
