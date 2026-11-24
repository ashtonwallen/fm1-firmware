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
  the player's settings shaped by activity and time of day, then coloured by the preset; `chord_choose`: the pad
  chord from the fish). PRESETS (1.1): `PRESETS[]` in `pond.c` (key, mode shift or pentatonic, tempo %, pad colour,
  air texture, deltas per parameter); `pd_root()` / `pd_mode()` are the key and mode heard. Mill Pond (0) is 1.0's sound.
- `app/ui.c` pages (Pond, Fish, Journal, Mix, Setup, World), drawing (the pond in two 88-row bands, the canvas holds
  240 x 144), autosave (`keep_due`, for flash wear: the world at most hourly, settings at most every 5 min, one save when
  the battery reads empty; hushes the audio 0.45 s around the flash write),
  help cards, screen reader. LFO / SEL step the presets (`preset_step`); the name shows in the header and on the
  Pond and Mix pages' rule.
- `app/project.c` the saved object (`project_t` in `app/app.h`, magic "POND", format 2; anything else gives a new pond).
  1.1 added `preset` in a byte 1.0 kept spare: format 1 loads whole, as Mill Pond. Only AMB-1's parameters are saved
  (`P_NSAVED`); 1.1's sounds come from the preset.
- `app/dsp/amb.c` AMB-1's engine with AMB-1 Drift 1.1's sounds ported (strings, lead, air, shimmer, Strings pad;
  `P_PLUCK`..`P_SHIMMER`, off at 0), plus `en_bell` (a fish's note, panned), `en_chord_root_to`, `en_hush`; 10 bell voices.
- `app/host.c` simulator checks and commands (`ff MIN` fast-forwards the pond, `heron`, `night`, `season N`,
  `storeamb`, `names`; 1.1: `storev1`, `snap`, expects `preset`, `format`, `sentN`, `same`, `fishsame`). Scenarios in `tests/scenarios/`.
- `web/` is AMB-1's browser app, not ported (its tests skip without emcc).
- USB name "POND FM-1" in `app/fm1.toml`: must keep "FM-1" in it so M-Flasher finds the unit.
