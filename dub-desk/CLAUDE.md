# DUB DESK

Endless generated roots dub for the M-VAVE FM-1, on fm1-base, built from LOFI RADIO's tree (itself from Charles
Vestal's AMB-1, MPL-2.0). New engine and UI; AMB-1's platform glue, knob strip, help cards and screen reader kept.
Flashing goes through `../flasher` (its own safety rules); releases are copied to `flasher/firmware/dub-desk/X.Y/`.

## Build (WSL Ubuntu)

From Git Bash: `MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu -- bash -lc 'cd /mnt/d/Projects/FM1/dub-desk && PYTHON=~/.venvs/fm1/bin/python <cmd>' | tr -d '\0'`

- `./build.sh` dev package `app/build/dubdesk.fwsc` (identity FM-1_9700)
- `./build.sh --release X.Y` release `app/build/dubdesk-X.Y.fwsc` (FM-1_97XXYYZZ); needs `docs/img/screen-desk.png`
- `./build.sh --host` the simulator; `sh tests/run_tests.sh` all tests (run `./build.sh` first for the update path)
- `tests/host/dub_test.c` also has `dump STYLE BARS` (every bar: block, mutes, chord, bass, melody, skank, organ,
  drums, the engineer's moves) and `levels STYLE` (each part alone). Use the dump to check the music by reading it.
- Engine cost: `dub_test` prints the busiest style's ns/frame; AMB-1 Drift's busiest piece (Low Tide) measured
  ~175-195 ns/frame on the same host, DUB DESK's busiest ~160-175.

Keep LF line endings. No doubles or libm in the engine (fastmath.h). No 64-bit division.

## Where things are

- Engine: `app/dsp/dub.c` / `dub.h`. Parameters (`PARAMS`, `P_*`), riddims (`RIDDIMS`: key, chords, the bass
  figure, drum styles), styles (`STYLES`: recipe, drums, snare, skank doubling, melodica/horn/siren chances, the
  engineer's temper, swing, riddims, arcs), the version plan (`BLOCKS`, `ARCS`, `plan()`: intro, 4-bar blocks of
  channel mutes and desk moves, outro), grooves and fills, melodica call/answer templates, horn stabs, voices
  (drums, sub bass, FM skank, organ, melodica, horns, siren), the desk (faders, sends and throws, tape echo, two-spring
  reverb, high-pass sweep, tone, leveller). Commands go main to render through a queue (`en_set`, `en_style`,
  `en_skip`, `en_throw`, `en_crash`...); the UI reads `en_*` state.
- New parameters go at the end of the `P_*` enum, before `P_NPARAMS`; that changes the saved project, so bump
  `PROJ_FORMAT` in `app/app.h` and carry the old format over in `app/project.c`.
- UI: `app/ui.c`. Pages Desk/Band/Room, the screen: tape echo loop (blips ride record to play head, ripples from a
  distance table in the pool), spring tank, scribble strip (riddim, version, chord, the engineer's move), eight
  channel strips; help cards `help_lines`, autosave (knobs rested 10 s, settings at most every 5 min, version at
  most hourly, only while stopped or in a version's last beats), screen reader `speak`.
- Project: `app/project.c` (format 1, magic "DUB1"); anything else in the store gives the defaults.
- Host checks/commands: `app/host.c`; scenarios in `tests/scenarios/`.
- Words for the screen reader: `app/words.tsv` (the url line is a placeholder; nothing is published yet).
- USB name "DUB DESK FM-1" in `app/fm1.toml`: must keep "FM-1" so M-Flasher finds the unit.
- Names: presets and riddims follow `../.claude-briefs/naming-rule.md` (no repeats across firmwares).
