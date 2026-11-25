# JAZZ TRIO

Endless generated jazz piano trio for the M-VAVE FM-1, on fm1-base, built from an earlier FM-1 firmware's tree (itself from
Charles Vestal's AMB-1, MPL-2.0). New engine and UI; AMB-1's platform glue, knob strip, help cards and screen reader
kept. Flashing goes through `../flasher` (its own safety rules); releases are copied to
`flasher/firmware/jazz-trio/X.Y/`.

## Build (WSL Ubuntu)

From Git Bash: `MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu -- bash -lc 'cd /mnt/d/Projects/FM1/jazz-trio && PYTHON=~/.venvs/fm1/bin/python <cmd>' | tr -d '\0'`

- `./build.sh` dev package `app/build/jazztrio.fwsc` (identity FM-1_9900)
- `./build.sh --release X.Y` release `app/build/jazztrio-X.Y.fwsc` (FM-1_99XXYYZZ); needs `docs/img/screen-club.png`
- `./build.sh --host` the simulator; `sh tests/run_tests.sh` all tests (run `./build.sh` first for the update path)
- Engine checks and cost: `tests/host/jazz_test.c` (the CPU line: busiest band in ns/frame, against AMB-1 Low Tide's
  ~168 ns/frame on the same host). Its other modes read the music: `jazz_test dump BAND TUNES [STRETCH]` prints every
  bar's chords, bass, melody and left hand; `jazz_test demo BAND SEC FILE [SKIP]` writes a WAV.

Keep LF line endings. No doubles or libm in the engine (fastmath.h). No 64-bit division.

## Where things are

- Engine: `app/dsp/jazz.c` (parameters, bands `STYLES`, kinds `KINDS`, the command queue, voices: FM piano,
  Karplus-Strong bass, synthesized kit and brushes, the crowd, reverb, bus), which includes:
  - `jazz_harm.c`: chord qualities, rootless A/B voicings, chord-progression templates (ii-V-I chains,
    turnarounds, blues, rhythm changes, modal, waltz), titles.
  - `jazz_tune.c`: a tune: its form, reharmonization, head melodies (lyric, bebop, riff, modal), the line player
    (`line_bar`, shared by solos and bebop heads), the arrangement (`arrange`: intro, head, solos, bass solo,
    trading fours, head out, ending) and the set (`pick_kind`, `new_tune`).
  - `jazz_play.c`: the players bar by bar (drums, walking/two-feel bass, comping and voice leading, the right
    hand), swing and timing (`swing_map`, `seq_beat`), applause and the gap between tunes.
- New parameters go at the end of the `P_*` enum, before `P_NPARAMS`; that changes the saved project, so bump
  `PROJ_FORMAT` in `app/app.h` and carry the old format over in `app/project.c`.
- UI: `app/ui.c`. Pages Band/Mix/Set; the club scene (`bg_build` draws the room once into the pool, then the
  spotlight, chalkboard, players and candle each frame), help cards `help_lines`, autosave (knobs rested 10 s,
  settings at most every 5 min, the set's position at most hourly, only on a break or between tunes), screen reader
  `speak`.
- Project: `app/project.c` (format 1, magic "JAZ1"); anything else in the store gives the defaults.
- Host checks/commands: `app/host.c`; scenarios in `tests/scenarios/`.
- Words for the screen reader: `app/words.tsv`.
- USB name "JAZZ TRIO FM-1" in `app/fm1.toml`: must keep "FM-1" so M-Flasher finds the unit.
- Names (bands, title words) must not repeat anywhere in the FM-1 repo: check with
  `rg -i -w "<name>" .. --glob '!**/base/**' --glob '!**/build/**'`.
