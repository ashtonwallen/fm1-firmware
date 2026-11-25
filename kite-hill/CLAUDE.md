# KITE HILL

A windy hilltop that plays itself, for the M-VAVE FM-1, on fm1-base. Started from FELT PIANO 1.0's tree (itself
from AMB-1 Drift, Charles Vestal's AMB-1, MPL-2.0): its platform glue, knob strip, help cards, screen reader,
room and bus kept; own engine and screen. Flashing goes through `../flasher` (its own safety rules); releases are
copied to `flasher/firmware/kite-hill/X.Y/`.

## Build (WSL Ubuntu)

From Git Bash: `MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu -- bash -lc 'cd /mnt/d/Projects/FM1/kite-hill && PYTHON=~/.venvs/fm1/bin/python <cmd>' | tr -d '\0'`

- `./build.sh` dev package `app/build/kitehill.fwsc` (identity FM-1_7200)
- `./build.sh --release X.Y` release `app/build/kitehill-X.Y.fwsc` (FM-1_72XXYYZZ); needs `docs/img/screen-hill.png`
- `./build.sh --host` the simulator; `sh tests/run_tests.sh` all tests (run `./build.sh` first for the update path)
- Engine tests alone: `cc -O2 -ffp-contract=off -std=c99 -DEN_HOST -Iapp/dsp -Ibase/include -o /tmp/kt tests/host/kite_test.c app/dsp/kite.c -lm`,
  then `/tmp/kt` (checks: every hill through a whole day, notes fit the chord, no seconds or minor ninths between
  lines, peaks, Ambience, cost), `/tmp/kt dump HILL MINUTES [DAY]` (launches, retunes, gusts, plucks, chords, and a
  snapshot every 15 s, to read as a musician), `/tmp/kt demos DIR` (one 20 s WAV per hill, settled, plus a 3 min
  listen from power-on)
- Engine cost: busiest hill ~85 ns/frame on this host against AMB-1 Drift's Low Tide ~160 ns/frame, timed the same
  way (six kites up, a big gust's plucks, Ambience and Drone full)

Keep LF line endings. No doubles or libm in the engine (fastmath.h). No 64-bit division.

## Where things are

- Engine: `app/dsp/kite.c` / `kite.h`.
  - Hills: `STYLES` (twelve): settings, drone tonic, kites' range, timbre (`TB_*`), just intonation flag (notes as
    harmonic numbers, `HSEMI`), sky, wind bias, gustiness, a ring of four chords (bass + notes as bits above the
    tonic). Names follow `.claude-briefs/naming-rule.md`.
  - World: `world()` per 256-frame chunk. Day curve `day_wind`, phases `PH_*`, gusts (`gust_start`: plucks, the
    evening's chord moves), launches (`pick`: chord tones, spread, no clashes), landing, breaking loose (`loose`),
    harmony (`harm_to`: nearest chord tone, gliding; a line with nowhere near is reeled in), new day (`KEY_WALK`).
  - Sound: `render_kites` (three partials + a breath resonator per line), `render_drone` (two crossfading slots),
    `render_plucks`, `render_room` (4-line FDN), `render_bed` (the wind: Ambience only), `render_bus` (leveller).
  - `EN_KITE[]` feeds the screen.
- New parameters go at the end of the `P_*` enum, before `P_NPARAMS`; that changes the saved project, so bump
  `PROJ_FORMAT` in `app/app.h` and carry the old format over in `app/project.c`.
- UI: `app/ui.c`. Pages Hill/Line/Setup, the scene (`draw_hill`, `draw_kite`, `draw_top`), header with HOLD and
  DAY, help cards `help_lines`, autosave (settings at most every 5 min, the world at most hourly, at a lull or
  resting), screen reader `speak` (each new part of the day queued).
- Project: `app/project.c` (format 1, magic "KIT1": settings, held weather, day, position, chord); anything else
  in the store gives the defaults.
- Host checks/commands: `app/host.c`; scenarios in `tests/scenarios/`; engine tests `tests/host/kite_test.c`.
- Words for the screen reader: `app/words.tsv` (the url line is a placeholder; nothing is published yet).
- USB name "KITE HILL FM-1" in `app/fm1.toml`: must keep "FM-1" so M-Flasher finds the unit.
