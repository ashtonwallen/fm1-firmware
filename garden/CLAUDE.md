# GARDEN

GARDEN: custom firmware for the M-VAVE FM-1. A melody garden that plays itself, with a gardener, bees, a rabbit and
weather. Built from `../amb1-drift` (branch `drift`, remote `template`): AMB-1's engine by Charles Vestal
(MPL-2.0) on fm1-base + felucca-bsp, with a world layer and a new screen. Flashing goes through
`../flasher` (its safety rules and M-Flasher); release builds are copied to its
`firmware/garden/<version>/`. Never flash or talk to the unit from this repo.

## Build (WSL Ubuntu)

Line endings must stay LF (`core.autocrlf false`, set here and in the submodules).

From Git Bash: `MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu -- bash -lc 'cd /mnt/d/Projects/FM1/garden && PYTHON=~/.venvs/fm1/bin/python <cmd>'`

- `./build.sh` dev package `app/build/garden.fwsc` (identity FM-1_7700)
- `./build.sh --release X.Y` release `app/build/garden-X.Y.fwsc` (FM-1_77XXYYZZ)
- `./build.sh --host` the simulator `app/build/host/garden_host`; `sh tests/run_tests.sh` all tests
- One scenario: `app/build/host/garden_host tests/scenarios/NAME.amb app/build/scenarios/NAME` (screenshots and WAVs land there)
- Don't run `python3 -` heredocs from Git Bash (they hang); write script files.

## Where things are

- `app/garden.c` the world: plants and their stages, phrases, the gardener, bees, rabbit, weather, day, event lines,
  and the music (each plot drives one engine plot; compost drives the drones; `music_apply` bends pad, brightness,
  bell tone, reverb, chord pace and the rain's hats from how full the garden is). PRESETS (1.1): `PRESETS[]` (key,
  mode shift or pentatonic, tempo %, air texture, deltas per parameter); `pv(p)` is what the engine gets (the player's
  value with the preset on it), `g_root()` / `g_pool()` the key and scale heard; `garden_preset` switches.
  Cottage (0) is 1.0's sound.
- `app/ui.c` the screen (header, scene, events or knob strip, ledger), controls, help cards, autosave (`keep_due`: the
  world at most hourly, settings at most every 5 min, once on an empty battery), screen reader. LFO / SEL step the
  presets (`preset_step`); the name shows in the header and on the knob strip's rule.
- `app/project.c` the saved project (`project_t` in `app/app.h`: settings + `world_t`), magic "GRDN" format 2;
  anything else in the store gives a new garden. 1.1 added `preset` in a byte 1.0 kept spare: format 1 loads whole,
  as Cottage. Only AMB-1's parameters are saved (`P_NSAVED`); 1.1's sounds come from the preset. Saved object is about 1.2 KB (limit 3840).
- `app/dsp/amb.c` AMB-1's engine with AMB-1 Drift 1.1's sounds ported (strings, lead, air, shimmer, Strings pad;
  `P_PLUCK`..`P_SHIMMER`, off at 0), plus GARDEN's plot layer (`en_plot`, `en_plot_note`: 12 phrase slots on the quarter
  grid, at most 3 notes a quarter) and two drones (`en_drone`). The engine's own loops are kept empty (Density 0);
  the thump is off (DRUM_KMAX[0] 0) so the pulse is only rain.
- `app/host.c` simulator checks (`expect living`, `stageN`, `hybrids`, `same` ...) and commands (`weather N`,
  `foreign`, `junk`, `clear`, `snap`, `age N`; 1.1: `storev1`, expects `preset`, `format`, `pvN`).
- `tests/scenarios/*.amb` the scenarios; `tests/host/amb_test.c` the engine's unit test.
- USB name "GARDEN FM-1" in `app/fm1.toml`: must keep "FM-1" so M-Flasher finds the unit. `install`/`repo` there
  are placeholders (a release needs https addresses).
