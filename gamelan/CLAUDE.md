# GAMELAN

Endless generated gamelan (Javanese / Balinese-inspired) for the M-VAVE FM-1, on fm1-base, built from AMB-1 Drift's
tree (Charles Vestal's AMB-1, MPL-2.0). New engine and UI; AMB-1's platform glue, knob strip, help cards, screen
reader and Dattorro plate kept. Flashing goes through `../flasher` (its own safety rules); releases are copied to
`flasher/firmware/gamelan/X.Y/`.

## Build (WSL Ubuntu)

From Git Bash: `MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu -- bash -lc 'cd /mnt/d/Projects/FM1/gamelan && PYTHON=~/.venvs/fm1/bin/python <cmd>' | tr -d '\0'`

- `./build.sh` dev package `app/build/gamelan.fwsc` (identity FM-1_9200)
- `./build.sh --release X.Y` release `app/build/gamelan-X.Y.fwsc` (FM-1_92XXYYZZ)
- `./build.sh --host` the simulator; `sh tests/run_tests.sh` all tests
- `GAM_BENCH=1 app/build/tests/gam_test` engine time per preset (AMB-1 Drift's busiest piece: ~0.0077 s/s on the
  same host; GAMELAN's busiest, Kotekan: ~0.0056)

Keep LF line endings. No doubles or libm in the engine (fastmath.h).

## Where things are

- Engine: `app/dsp/gam.c` / `gam.h`. Tunings (`EN_CENTS`, `STRONG` pathet tones), forms (`FORMS`: beats and
  colotomic bitmasks), presets (`PRESETS`, one value per `P_*`), balungan generation (`gen_section`,
  `fill_gatra`, `SHAPES`), elaboration (`kot_figure`, peking and bonang figures in `beat_start`), kendang patterns
  (`KP_*`), the piece lifecycle (`new_piece`, `decide_next`, `gatra_start`, `finish`: buka, play, suwuk, gap),
  irama (`irama_request`, the switch in `beat_start`: laya ramps until the grid doubles or halves), voices
  (`strike`: modal partials as complex phasors, `mv_run`), kendang and suling synths, plate.
- New parameters go at the end of the `P_*` enum, before `P_NPARAMS`; that changes the saved project, so bump
  `PROJ_FORMAT` in `app/app.h` and carry the old format over in `app/project.c`.
- UI: `app/ui.c`. Pages `VIEW_KNOB`/`KNOB_LABEL`, palettes `PAL[]` (one per preset), mandala `draw_mandala`,
  corners `draw_corners`, help `help_lines`, autosave `autosave` (knobs rested 4 s, at most every 5 min, only while
  resting or in the gap between pieces; held in one piece, after 20 min as a gong rings), screen reader `speak`.
- Project: `app/project.c` (format 1, magic "GML1"); anything else in the store gives the defaults.
- Host checks/commands: `app/host.c`; scenarios in `tests/scenarios/`.
- Words for the screen reader: `app/words.tsv` (the url line is a placeholder; nothing is published yet).
