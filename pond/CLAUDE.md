# POND

POND: custom firmware for M-VAVE FM-1. A pond of named fish (species = voice, temperament, a note each) that
sing, meet, make friends and rivals, have fry and grow old, over days and seasons; a heron, frogs at night. Built on
AMB-1 Drift (`../amb1-drift`, remote `template`), which is Charles Vestal's AMB-1 (MPL-2.0) on fm1-base.
Flashing goes through `../flasher` (its safety rules and flasher app): copy release builds into its
`firmware/pond/X.Y/`. Never flash from here.

## Build (WSL Ubuntu)

From Git Bash: `MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu -- bash -lc 'cd /mnt/d/Projects/FM1/pond && PYTHON=~/.venvs/fm1/bin/python <cmd>' | tr -d '\0'`

- `./build.sh` dev package `app/build/pond.fwsc` (identity FM-1_7600)
- `./build.sh --release X.Y` release `app/build/pond-X.Y.fwsc` (FM-1_76XXYYZZ)
- `./build.sh --host` the simulator `app/build/host/pond_host`; `sh tests/run_tests.sh` all tests
- Line endings stay LF (`core.autocrlf false`). No libc on the device (no strncmp, memchr): write helpers.

## Where things are

- `app/pond.h` what is saved of the world (`world_t`: fish, relationships, lily pads, journal); `app/pond.c` the
  simulation (steering, meetings, food, fry, deaths, the clock, the heron) and how the music follows it (`pd_sound`:
  the player's settings shaped by activity and time of day; `chord_choose`: the pad chord from the fish).
- `app/ui.c` pages (Pond, Fish, Journal, Mix, Setup, World), drawing (the pond in two 88-row bands, the canvas holds
  240 x 144), autosave (`keep_due`, for flash wear: the world at most hourly, settings at most every 5 min, one save when
  the battery reads empty; hushes the audio 0.45 s around the flash write),
  help cards, screen reader.
- `app/project.c` the saved object (`project_t` in `app/app.h`, magic "POND", format 1; anything else gives a new pond).
- `app/dsp/amb.c` AMB-1's engine plus `en_bell` (a fish's note, panned), `en_chord_root_to`, `en_hush`; 10 bell voices.
- `app/host.c` simulator checks and commands (`ff MIN` fast-forwards the pond, `heron`, `night`, `season N`,
  `storeamb`, `names`). Scenarios in `tests/scenarios/`.
- `web/` is AMB-1's browser app, not ported (its tests skip without emcc).
- USB name "POND FM-1" in `app/fm1.toml`: must keep "FM-1" in it so M-Flasher finds the unit.
