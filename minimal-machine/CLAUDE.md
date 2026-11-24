# MINIMAL MACHINE

Endless minimal techno for the FM-1: six polymetric loops (kick, hats, perc, stab, bass, bleep) that phase and
mutate one step at a time. Built from AMB-1 Drift's app skeleton (fm1-base app, MPL-2.0) with a new engine and screen.
Identity: name MINIMAL, id_prefix FM-1_91 (dev FM-1_9100), USB "MINIMAL MACHINE FM-1" (must keep "FM-1"). Flasher
hand-off id `minimal-machine`.

## Build (WSL Ubuntu)

From Git Bash: `MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu -- bash -lc 'cd /mnt/d/Projects/FM1/minimal-machine && PYTHON=~/.venvs/fm1/bin/python <cmd>' | tr -d '\0'`

- `./build.sh` dev package `app/build/minimal.fwsc`
- `./build.sh --release X.Y` release `app/build/minimal-X.Y.fwsc`
- `./build.sh --host` the simulator `app/build/host/minimal_host SCRIPT OUTDIR`
- `sh tests/run_tests.sh` all tests: engine (`tests/host/mm_test.c`, also times each preset with `MM_TIME=1` and
  writes a minute of each as WAV when given a directory), update path, scenarios (`tests/scenarios/*.amb`)

## Where things are

- Engine: `app/dsp/mm.c` / `mm.h`. Parameters `P_*` (scenarios use their numbers: keep the order). Presets:
  `PRESETS[]` in `mm.c` (knob defaults, one pattern string per part, chord, sound character); count `MM_NPRESETS`.
  Pattern syntax is in the comment above `preset_t`.
- Loops: each step has a priority; Density sets the threshold (`thr`). Mutations (`mutate`, `mu_*`) at bar starts,
  every `mut_every(Drift)` bars. The kick stays 16 steps (`mm_gen_ok` checks it).
- Mix and safety: soft limiter at 0.7 in `render_chunk`; `mm_lim_min` reports its gain reduction (tests).
- Saving: `app/ui.c` `autosave` (settings: knobs rested 5 s and 5 min since last save; loops: hourly), each in a
  quiet moment the engine makes on request (`mm_hush`: last sixteenth of a bar faded). Project in `app/app.h`
  (`PROJ_MAGIC` "MNM1", format 1), `app/project.c`. Evolved loops (`mm_gen_t`) are saved for the playing preset.
- Screen: `app/ui.c` `draw_machine` (rings, read head, mutation flash), knob strip, help cards, screen reader
  (`speak`, `speak_dump`). Words in `app/words.tsv`.
- Host checks and commands: top of `app/host.c` (`gen_mark` / `gen_same`, `store_other`, `autosave_skip`).

## Budgets (1.0)

RAM .data+.bss ~32.6 KB of 98, pool ~288 KB of 344 (echo 2 x 44200 int16, room 10.5k floats, canvas). Host CPU
~0.8x AMB-1 Drift's busiest piece.
