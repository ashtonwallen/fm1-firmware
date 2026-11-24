# DECAY LOOPS

FM-1 firmware: a tape looper whose loops disintegrate (after Basinski's Disintegration Loops). Built from AMB-1 Drift's
app on fm1-base (`base/`, submodule). MPL-2.0; credit Charles Vestal (AMB-1) and fm1-base / Felucca. Public repo: no
personal names, paths or gear in files. Flashing goes through `../flasher` (its own rules); never from here.

## Build (WSL Ubuntu)

From Git Bash: `MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu -- bash -lc 'cd /mnt/d/Projects/FM1/decay-loops && PYTHON=~/.venvs/fm1/bin/python <cmd>' | tr -d '\0'`

One-time setup inside WSL: `bash .wsl-setup.sh` (toolchain, three SDK files, a Pillow venv).

- `./build.sh` dev package `app/build/decayloops.fwsc` (identity FM-1_8800)
- `./build.sh --release X.Y` release `app/build/decayloops-X.Y.fwsc` (FM-1_88XXYYZZ)
- `./build.sh --host` simulator `app/build/host/decayloops_host SCRIPT OUTDIR`
- `sh tests/run_tests.sh` all tests (engine test, update path, scenarios)

Budgets the build checks: RAM .data+.bss 98 KB, pool 344 KB with 8 KB headroom, no doubles / libm / 64-bit division.
Keep files LF.

## Where things are

- Engine: `app/dsp/decay.c` / `decay.h`. Commands posted from the UI, drained by `en_render` (audio ISR).
  - Phrase synth (7 voices, `VENV` gains balance them), phrases from AMB-1's harmony (`en_chord_tones`, progressions
    mutated per loop).
  - Tape: one ring of mu-law bytes at 11025 Hz (`EN_TAPE_N`, 27 s), FIFO; no room retires the oldest loop. Recorded
    through a 4.4 kHz anti-alias filter and tanh drive (`REC_DRIVE`), the tail spliced into the start (`XF`).
  - Wear (`layer_control`): 128-cell wear map with seeded growing holes per loop (`en_lay_dmg`), low-pass falling to
    300 Hz, high-pass rising, wow / flutter / sag, dips, level and a fade at the end; hiss and crackle from all layers.
  - Spawn (`spawn_check`): a new loop when the newest is worn 1/Layers of the way, on its downbeat.
  - Presets: `PRESETS[]` in `decay.c`, `EN_NPRESETS`; stocks in `STOCK[]`. Parameter order is `P_*` (scenarios use the
    numbers: `parN`).
  - Plate reverb at 22.05 kHz in int16 RAM (35 KB) so the pool is the tape's.
- UI: `app/ui.c`. Deck drawn in two 72-row bands (`APP_CANVAS_PX` in `app.h` halves the canvas to free pool for the
  tape). Reels, tape run, strips, flakes; pages Tape / Tone / Phrase / Setup; help cards; screen reader (`speak`,
  `speak_dump`); autosave (rested 10 s, at most every 5 min, at a dip in the output).
- Saved data: `app/project.c` / `app.h` (`PROJ_MAGIC` "DCY1", format 1): settings and each preset's slot only; loops
  are not saved. Anything else in the store gives defaults.
- Host checks and commands: `app/host.c` (`storeamb`, `storeold`, `battery`).
- Tests: `tests/host/dl_test.c` (engine; also `wav` and `bench` modes), `tests/scenarios/*.amb`.
- Screen reader words: `app/words.tsv` (url is a placeholder until published).
- USB name "DECAY LOOPS FM-1" in `app/fm1.toml`: keep "FM-1" in it so M-Flasher finds the unit.
