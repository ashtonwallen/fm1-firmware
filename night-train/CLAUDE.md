# NIGHT TRAIN

NIGHT TRAIN: firmware for M-VAVE FM-1. An endless, seeded night journey (landscapes, stations, six
passengers with stories) driving AMB-1's generative ambient engine (charlesvestal/fm1-amb, MPL-2.0). Started from
`../amb1-drift` (remote `template`, branch `drift`); local only, branch `main`. Flashing goes through
`../flasher` (its safety rules and M-Flasher); release builds are copied to its
`firmware/night-train/X.Y/`. Never flash or talk to the unit from here.

## Build (WSL Ubuntu)

One-time setup: `bash .wsl-setup.sh` inside WSL (JieLi toolchain in `~/.jieli`, three AC79 SDK files in
`~/fw-AC79_AIoT_SDK`, Pillow in `~/.venvs/fm1`). The repo must keep LF line endings (`core.autocrlf false`, set).

From Git Bash: `MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu -- bash -lc 'cd /mnt/d/Projects/FM1/night-train && PYTHON=~/.venvs/fm1/bin/python <cmd>'`
(pipe through `tr -d '\0'`).

- `./build.sh` dev package `app/build/nighttrain.fwsc` (identity FM-1_7800)
- `./build.sh --release X.Y` release `app/build/nighttrain-X.Y.fwsc` (FM-1_78XXYYZZ)
- `./build.sh --host` the simulator; `sh tests/run_tests.sh` all tests

The device has no libc string functions beyond memset/memcpy/memcmp (no strlen, strcpy): `world.c` includes
`<string.h>` only when compiled alone for the host test. No doubles, no 64-bit division (the build checks).

## Where things are

- `app/world.c`, `app/world.h`: the world, plain C, no platform calls. Route (`land_of`, `station_name`), the
  train's physics and phases (RUN, BRAKE, DWELL, SIGNAL), passengers (`who_t`), story beats (`w_BEAT`, epilogues
  `w_EPI`, talk templates), the journal (`logrec_t` codes, words made by `world_text`). Statics are `w_`-prefixed
  (the unity build shares one namespace).
- `app/ui.c`: input, the music conductor (`music_targets`: landscape recipe + speed, stop, tunnel, bridge, rain,
  Theo; slides 10x a second; key changes at the station in `music_swap`), saving (`keep_due`, for flash wear: the
  journey at most hourly, settings at most every 5 min, once on an empty battery; at stations or standing: `journey`,
  `autosave`), drawing
  (window: sky, far, mid, near layers, tunnel, bridge, platform, weather, seats; info strip; journal, passengers,
  setup pages), help cards, screen reader.
- `app/dsp/amb.c`: AMB-1's engine. Ours: `PIECES[]` are the eight landscapes (order = `LS_*`), params `P_RAIL`,
  `P_RAIN`, `P_RUMBLE`, `en_chime`, `en_bell` (the NIGHT TRAIN section: rail clicks on the grid, rumble, rain).
  AMB-1 Drift 1.1's sounds are ported in (strings, lead, air, shimmer, the Strings pad: params `P_PLUCK`..`P_SHIMMER`
  after `P_RUMBLE`, all off in the landscapes' recipes).
- Lines (1.1): `ui.c` `LINES[]` (12, `NLINES` = `PROJ_NLINES` in `app.h`), applied by `line_apply` in
  `music_targets` over the landscape's recipe: pad colour, mode, tempo %, levels %, Drift 1.1's sounds, space, clack.
  Night Line (0) changes nothing (1.0's sound). FX steps, Setup K4 picks; `music_tick` swaps the discrete parts at
  once (`mus_line`). The world is never touched.
- `app/project.c`, `app/app.h`: one store object, magic "NTR1", format 2 (settings + `world_t`, ~1.4 KB of 3840).
  Format 2 (1.1) puts the line in 1.0's spare byte; a format 1 project loads whole on line 0.
- `app/host.c`: simulator checks and commands (`ff MIN` runs the world alone, `land N`, `tunnel`, `bridge`,
  `rain N`, `express`, `storeold`, `storev1` (1.0's format), `story`; checks `line`, `format`).
- Tests: `tests/host/amb_test.c` (engine), `tests/host/world_test.c` (30 h of journey), `tests/scenarios/*.amb` (`lines.amb`: every line sounds, peaks, the world kept, format 1).
- USB name "NIGHT TRAIN FM-1" in `app/fm1.toml`: keep "FM-1" in it so M-Flasher finds the unit.
