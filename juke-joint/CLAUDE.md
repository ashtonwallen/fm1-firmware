# JUKE JOINT

Endless blues trio (piano, upright bass, brushes, a guest harmonica) for the M-VAVE FM-1, on fm1-base, built from
FELT PIANO's tree (itself from Charles Vestal's AMB-1, MPL-2.0). Own engine and screen; AMB-1's platform glue, knob
strip, help cards and screen reader kept. Flashing goes through `../flasher` (its own safety rules); releases are
copied to `flasher/firmware/juke-joint/X.Y/`.

## Build (WSL Ubuntu)

From Git Bash: `MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu -- bash -lc 'cd /mnt/d/Projects/FM1/juke-joint && PYTHON=~/.venvs/fm1/bin/python <cmd>' | tr -d '\0'`
(wsl.exe expands `$`: put loops in a script file and run that).

- `./build.sh` dev package `app/build/jukejoint.fwsc` (identity FM-1_5100)
- `./build.sh --release X.Y` release `app/build/jukejoint-X.Y.fwsc` (FM-1_51XXYYZZ); needs `docs/img/screen-stage.png`
- `./build.sh --host` the simulator; `sh tests/run_tests.sh` all tests (run `./build.sh` first for the update path)
- Engine tests alone: `cc -O2 -ffp-contract=off -std=c99 -DEN_HOST -Iapp/dsp -Ibase/include -o /tmp/jt tests/host/juke_test.c app/dsp/juke.c -lm`,
  then `/tmp/jt` (checks, levels, cost per night), `/tmp/jt dump NIGHT SEED [DRUMS]` (what a tune plays, bar by
  bar: B bass, P piano right hand, L left hand, c comping, H harp), `/tmp/jt demos DIR` (one 20 s WAV per night
  plus a 3 min listen from power-on), `/tmp/jt wav NIGHT SEED SEC FILE`
- Engine cost: time `en_render` on the host against AMB-1 Drift's busiest piece (Low Tide, ~168 ns/frame). Busiest
  night is about 124 ns/frame

Keep LF line endings. No doubles or libm in the engine (fastmath.h). No 64-bit division.

## Where things are

- Engine: `app/dsp/juke.c` (voices, mixer, commands, titles, nights) includes `app/dsp/juke_band.c` (the band:
  forms, licks, players, arrangement). One `-O2` unit.
  - Piano: FELT PIANO's modal voice (12 voices), sustain pedal. Bass: Karplus-Strong pluck, damped by the next
    note. Drums: brush slaps, taps and the sweep, feathered kick, hi-hat foot, stick clicks for count-offs. Harp:
    band-limited pulse and saw through a cupped-hands filter, breath, bends, shakes, falls, vibrato, chords, amp
    drive. Crowd: murmur, glass clinks, applause (Ambience). 4-line FDN room, Age wobble, leveller and tanh.
  - Composer: bar by bar into an event queue, 12 ticks a beat. Kinds `K_SLOW` (12/8), `K_SHUF`, `K_MINOR` (even
    4/4), `K_BALLAD` (8 bars). Forms `F_BLUES`, `F_MINOR`, `F_BALLAD`; piano licks `LICK`/`MLICK` (fitted to the
    chord by `bl_fit`), harp licks `HLICK`/`MHLICK` (cross harp, `H2_OK`, fitted by `h_fit`). Players
    `drums_bar`, `bass_bar` (two-beat, walk, boogie, minor), `comp`, `lh`, `piano_bar`, `harp_bar`, `count_bar`.
    Arrangement in `new_tune`: intro type, heads, piano and harp solos, stop-time chorus, out chorus, ending
    (`END_B2`, `END_6`, `END_MIN`), applause and gap. The set's first tune is the night's own kind.
  - Nights: `STYLES` (ten settings, kind weights, felt, amp, jazz changes, stop-time chance, scene). Names follow
    `.claude-briefs/naming-rule.md` (no name used elsewhere in the repo). New nights go at the end
    (`EN_NSTYLES`); the slots are saved, so adding nights changes the project format.
- New parameters go at the end of the `P_*` enum, before `P_NPARAMS`; that changes the saved project, so bump
  `PROJ_FORMAT` in `app/app.h` and carry the old format over in `app/project.c`.
- UI: `app/ui.c`. Pages Band/Mix/Setup, ALGORITHM = Harp. Stage (`bg_build` once per room, then `draw_piano`,
  `draw_harp`, `draw_bass`, `draw_drums`, `draw_bulb`, `draw_crowd`, `draw_top`, `draw_form` every frame), header
  with HARP and REPEAT pills, help cards `help_lines`, autosave (knobs rested 10 s, settings at most every 5 min,
  tune position at most hourly, only while resting or at a tune's ending or the break), screen reader `speak`.
- Project: `app/project.c` (format 1, magic "JKJ1"); anything else in the store gives the defaults.
- Host checks/commands: `app/host.c`; scenarios in `tests/scenarios/`; engine tests `tests/host/juke_test.c`.
- Words for the screen reader: `app/words.tsv`, `app/words.allow`.
- USB name "JUKE JOINT FM-1" in `app/fm1.toml`: must keep "FM-1" so M-Flasher finds the unit.
