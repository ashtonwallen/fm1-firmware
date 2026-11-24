# LOFI RADIO

Endless generated lo-fi hip-hop for the M-VAVE FM-1, on fm1-base, built from AMB-1 Drift's tree (Charles Vestal's
AMB-1, MPL-2.0). New engine and UI; AMB-1's platform glue, knob strip, help cards and screen reader kept. Flashing
goes through `../flasher` (its own safety rules); releases are copied to `flasher/firmware/lofi-radio/X.Y/`.

## Build (WSL Ubuntu)

From Git Bash: `MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu -- bash -lc 'cd /mnt/d/Projects/FM1/lofi-radio && PYTHON=~/.venvs/fm1/bin/python <cmd>' | tr -d '\0'`

- `./build.sh` dev package `app/build/lofiradio.fwsc` (identity FM-1_8900)
- `./build.sh --release X.Y` release `app/build/lofiradio-X.Y.fwsc` (FM-1_89XXYYZZ); needs `docs/img/screen-radio.png`
- `./build.sh --host` the simulator; `sh tests/run_tests.sh` all tests (run `./build.sh` first for the update path)
- Engine cost: time `en_render` on the host against AMB-1 Drift's busiest piece (Low Tide, ~168 ns/frame); LOFI
  RADIO's busiest style measured ~170 ns/frame on the same host

Keep LF line endings. No doubles or libm in the engine (fastmath.h). No 64-bit division.

## Where things are

- Engine: `app/dsp/lofi.c` / `lofi.h`. Parameters (`PARAMS`, `P_*`), styles (`STYLES`: recipe, keys timbre, kit,
  melody voice, laid-back time, groove), chord qualities and rootless voicings, progressions (with mood and
  major/minor), grooves, comping and bass patterns, track plan (intro, A, B, breakdown, outro, runout), titles
  (`en_title`), voices (FM electric piano, bass, drums, melody), crackle/rain, wobble, echo, FDN reverb, tone,
  leveller. Commands go main to render through a queue (`en_set`, `en_style`, `en_skip`...); UI reads `en_*` state.
- New parameters go at the end of the `P_*` enum, before `P_NPARAMS`; that changes the saved project, so bump
  `PROJ_FORMAT` in `app/app.h` and carry the old format over in `app/project.c`.
- UI: `app/ui.c`. Pages Radio/Mix/Room, turntable (`REC_*`, precomputed record tables in the pool), tonearm, VU,
  help cards `help_lines`, autosave (knobs rested 10 s, settings at most every 5 min, track position at most
  hourly, only while paused or in a runout), screen reader `speak`.
- Project: `app/project.c` (format 1, magic "LOF1"); anything else in the store gives the defaults.
- Host checks/commands: `app/host.c`; scenarios in `tests/scenarios/`; engine tests `tests/host/lofi_test.c`.
- Words for the screen reader: `app/words.tsv` (the url line is a placeholder; nothing is published yet).
- USB name "LOFI RADIO FM-1" in `app/fm1.toml`: must keep "FM-1" so M-Flasher finds the unit.
