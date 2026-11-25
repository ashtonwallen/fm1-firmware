# BOSSA LOUNGE

Endless generated bossa nova and lounge jazz for the M-VAVE FM-1, on fm1-base, built from AMB-1 Drift's tree (Charles
Vestal's AMB-1, MPL-2.0). New engine and screen; AMB-1's platform glue, knob strip, help cards
and screen reader kept. Flashing goes through `../flasher` (its own safety rules); releases are copied to
`flasher/firmware/bossa-lounge/X.Y/`.

## Build (WSL Ubuntu)

From Git Bash: `MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu -- bash -lc 'cd /mnt/d/Projects/FM1/bossa-lounge && PYTHON=~/.venvs/fm1/bin/python <cmd>' | tr -d '\0'`

- `./build.sh` dev package `app/build/bossalounge.fwsc` (identity FM-1_9400)
- `./build.sh --release X.Y` release `app/build/bossalounge-X.Y.fwsc` (FM-1_94XXYYZZ); needs `docs/img/screen-lounge.png`
- `./build.sh --host` the simulator; `sh tests/run_tests.sh` all tests (run `./build.sh` first for the update path)
- Engine cost: `tests/host/bossa_test.c` prints the busiest style's ns/frame; compare with AMB-1 Drift's busiest
  piece (Low Tide, ~170 ns/frame on the same host). BOSSA LOUNGE measured ~130-140 ns/frame.
- Read the music: `bossa_test dump STYLE N` prints N tunes bar by bar (chords, bass, melody). Do this after any
  change to forms, voicing or the melody, not just the numbers.

Keep LF line endings. No doubles or libm in the engine (fastmath.h). No 64-bit division.

## Where things are

- Engine: `app/dsp/bossa.c` / `bossa.h`.
  - `PARAMS`, `P_*`; `STYLES` (recipe plus guitar figures, bass figures, kit, melody cells, neon colour).
  - Chord qualities `Q_*` with tones, tensions, scales and guitar voicings; `FORMS` (15 curated forms: sections A,
    a (A ending home), B, C; chords in half bars, optional slash bass). `en_check_forms` checks they add up.
  - Tune plan: intro (the form's last 4 bars), head, solo chorus(es), head out, 6-bar tag ending, 3 s gap.
    `mod` lifts the last chorus a half step. Repeat loops to the first solo chorus.
  - Melody: `gen_phrase` / `pick_pitch` (rhythm cells, chord tones on strong beats, steps, cadences). Head phrases
    share a seed so A sections repeat.
  - Voices: Karplus-Strong nylon guitar, FM upright bass, brushes/rim/kick, flute, vibes (also the player's keys),
    FDN reverb, tone, leveller.
  - Commands go main to render through a queue (`en_set`, `en_style`, `en_skip`, ...); the UI reads `en_*` state.
- New parameters go at the end of the `P_*` enum, before `P_NPARAMS`; that changes the saved project, so bump
  `PROJ_FORMAT` in `app/app.h` and carry the old format over in `app/project.c`.
- UI: `app/ui.c`. Pages Tune/Combo/Room. Screen: wall with neon glass (`draw_wall`, `draw_neon`, glow table in the
  pool), title card, deck with reels (`draw_reel`, radius/angle tables in the pool; `tape_tick` turns them at tape
  speed over pack radius, rewinds in the gap), amp with chord window and two VUs. Help cards `help_lines`, autosave
  (knobs rested 10 s, settings at most every 5 min, tune position at most hourly, only stopped or in the gap),
  screen reader `speak`.
- Project: `app/project.c` (format 1, magic "BSL1"); anything else in the store gives the defaults.
- Host checks/commands: `app/host.c`; scenarios in `tests/scenarios/`; engine tests `tests/host/bossa_test.c`.
- Words for the screen reader: `app/words.tsv` (the url line is a placeholder; nothing is published yet).
- USB name "BOSSA LOUNGE FM-1" in `app/fm1.toml`: must keep "FM-1" so M-Flasher finds the unit.
- Names: preset names and title words must not repeat anything elsewhere in the FM1 repo (see the naming rule
  in the briefs); check with `rg -i -w` before adding one.
