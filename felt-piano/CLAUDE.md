# FELT PIANO

Endless generated solo piano for the M-VAVE FM-1, on fm1-base, built from AMB-1 Drift's tree (Charles Vestal's
AMB-1, MPL-2.0). Own engine and screen; AMB-1's platform glue, knob strip, help cards and screen reader kept. Flashing
goes through `../flasher` (its own safety rules); releases are copied to `flasher/firmware/felt-piano/X.Y/`.

## Build (WSL Ubuntu)

From Git Bash: `MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu -- bash -lc 'cd /mnt/d/Projects/FM1/felt-piano && PYTHON=~/.venvs/fm1/bin/python <cmd>' | tr -d '\0'`

- `./build.sh` dev package `app/build/feltpiano.fwsc` (identity FM-1_9600)
- `./build.sh --release X.Y` release `app/build/feltpiano-X.Y.fwsc` (FM-1_96XXYYZZ); needs `docs/img/screen-roll.png`
- `./build.sh --host` the simulator; `sh tests/run_tests.sh` all tests (run `./build.sh` first for the update path)
- Engine tests alone: `cc -O2 -ffp-contract=off -std=c99 -DEN_HOST -Iapp/dsp -Ibase/include -o /tmp/ft tests/host/felt_test.c app/dsp/felt.c -lm`,
  then `/tmp/ft` (checks, levels, cost per style), `/tmp/ft dump STYLE SEED BARS` (what a piece plays, bar by bar),
  `/tmp/ft demos DIR` (one 20 s WAV per style plus a 3 min listen from power-on)
- Engine cost: time `en_render` on the host against AMB-1 Drift's busiest piece (Low Tide, ~168 ns/frame). Busiest
  FELT PIANO style is about the same; sixteen voices held with the pedal is the worst case (~180 ns/frame)

Keep LF line endings. No doubles or libm in the engine (fastmath.h). No 64-bit division.

## Where things are

- Engine: `app/dsp/felt.c` / `felt.h`.
  - Piano voice: modal synthesis, 8 two-pole resonators per note (partials 1,1,2,2,3..6, doubles detuned), stretch
    and T60 by register, felt as a low-pass on partial amplitudes plus a soft attack, dampers (none above note 88),
    sustain pedal. 16 voices; quietest stolen; top partials culled once inaudible.
  - Body: hammer/key/damper/pedal knocks, six sympathetic strings tuned to the harmony, 4-line FDN room, Age wobble
    and hiss, leveller.
  - Composer: bar by bar into an event queue. Idioms `gym_bar`, `air_bar`, `ost_bar`, `noct_bar`, `modal_bar`;
    melody picker `mel_pick` (steps, leap recovery, chord tones on strong beats, contour, kept themes); piece flow
    intro/body/end/gap; titles `en_title`.
  - Styles: `STYLES` (ten settings, idiom, variant, register, modes). Names follow `.claude-briefs/naming-rule.md`
    (no name used elsewhere in the repo).
  - Note and pedal logs (`EN_LOG`, `EN_PED`) feed the screen.
- New parameters go at the end of the `P_*` enum, before `P_NPARAMS`; that changes the saved project, so bump
  `PROJ_FORMAT` in `app/app.h` and carry the old format over in `app/project.c`.
- UI: `app/ui.c`. Pages Piece/Touch/Setup, piano roll (`draw_roll`, `draw_top`), header with PED and REPEAT pills,
  help cards `help_lines`, autosave (knobs rested 10 s, settings at most every 5 min, piece position at most hourly,
  only while resting or at a piece's close or gap), screen reader `speak`.
- Project: `app/project.c` (format 1, magic "FLT1"); anything else in the store gives the defaults.
- Host checks/commands: `app/host.c`; scenarios in `tests/scenarios/`; engine tests `tests/host/felt_test.c`.
- Words for the screen reader: `app/words.tsv` (the url line is a placeholder; nothing is published yet).
- USB name "FELT PIANO FM-1" in `app/fm1.toml`: must keep "FM-1" so M-Flasher finds the unit.
