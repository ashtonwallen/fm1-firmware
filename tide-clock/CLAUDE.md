# TIDE CLOCK

TIDE CLOCK: firmware for M-VAVE FM-1. An ambient piece tied to the time of day (AMB-1's engine) with a
coastal desk sim on the screen: tides, boats, gulls, weather, and Abel the lighthouse keeper with a mood and a log.
Cloned from `../amb1-drift` (remote `template`, branch `drift`); our branch is `main`. MPL-2.0.
Flashing goes through `../flasher` (its safety rules and flasher app); never flash from here.
Release packages are copied to `../flasher/firmware/tide-clock\<version>\`; then they are added to its
catalog and log.

## Build (WSL Ubuntu)

From Git Bash: `MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu -- bash -lc 'cd /mnt/d/Projects/FM1/tide-clock && PYTHON=~/.venvs/fm1/bin/python <cmd>'`
(pipe through `tr -d '\0'`). The repo must keep LF line endings (`core.autocrlf false`, set).

- `./build.sh` dev package `app/build/tideclock.fwsc` (identity FM-1_7500)
- `./build.sh --release X.Y` release `app/build/tideclock-X.Y.fwsc` (FM-1_75XXYYZZ)
- `./build.sh --host` the simulator; `sh tests/run_tests.sh` all tests; `sh tests/run_some.sh NAME...` a few
  scenarios quickly (only failures printed). Screenshots and WAVs land in `app/build/scenarios/<name>/`.
- In scenarios: `settime HH MM`, `weather calm|fog|wind|storm [MIN]`, `storeamb`, `seed N`, and expects listed at the
  top of `app/host.c`.

## Where things are

- `app/dsp/amb.c`: AMB-1's engine. `PIECES[]` are the day's eight phases (Night .. Dusk), one value per parameter;
  `render_sea` is the sea layer (params `P_SEA`, `P_SURF`). 1.1 ported AMB-1 Drift 1.1's sounds (strings, lead, air,
  shimmer, Strings pad: `P_PLUCK`..`P_SHIMMER`, off at 0); every phase has them off (`D11_OFF`).
- `app/world.c`: the world clock (Clock / Cycle), weather, boats, keeper, Captain, the log, and the conductor that
  blends phase recipes plus weather/tide/mood into engine targets and steps toward them (key changes on a new chord,
  at most every 15 s). PRESETS (1.1): `PRESETS[]` in `world.c` recolour the phase recipes in `targets()` (key, mode
  shift or pentatonic, tempo %, pad colour, air texture, deltas per parameter); `preset_set` moves to one quickly
  (8 s) with the key at once. Harbour (0) is 1.0's sound. The world is not touched.
- `app/ui.c`: the scene (drawn in two 240x120 bands), log page, setup page, clock-setting screen, help cards,
  autosave, screen reader. PRESETS (the encoder) turns presets (`preset_turn`); the name shows on the panel and the
  setup page.
- `app/project.c` / `app/app.h`: the saved project (`PROJ_MAGIC` "TIDK", format 2: 1.1 added `preset` in a byte 1.0
  kept spare; format 1 loads whole, as Harbour). Anything else in the store gives defaults. Host `storev1` saves the
  current project as format 1; `tests/scenarios/presets.amb` covers presets, peaks and the migration.
- USB name "TIDE CLOCK FM-1" in `app/fm1.toml`: must keep "FM-1" so M-Flasher finds the unit.
- Flash wear (`keep_due` in `app/ui.c`): the world and the time are saved at most once an hour, settings 5 s after a
  change but at most every 5 min, and once when the battery reads empty; saves wait for a quiet moment in the music.
  A power-off can lose up to an hour.
