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
  `render_sea` is the sea layer (params `P_SEA`, `P_SURF`).
- `app/world.c`: the world clock (Clock / Cycle), weather, boats, keeper, Captain, the log, and the conductor that
  blends phase recipes plus weather/tide/mood into engine targets and steps toward them (key changes on a new chord,
  at most every 15 s).
- `app/ui.c`: the scene (drawn in two 240x120 bands), log page, setup page, clock-setting screen, help cards,
  autosave, screen reader.
- `app/project.c` / `app/app.h`: the saved project (`PROJ_MAGIC` "TIDK", format 1). Anything else in the store gives
  defaults.
- USB name "TIDE CLOCK FM-1" in `app/fm1.toml`: must keep "FM-1" so M-Flasher finds the unit.
- Flash wear (`keep_due` in `app/ui.c`): the world and the time are saved at most once an hour, settings 5 s after a
  change but at most every 5 min, and once when the battery reads empty; saves wait for a quiet moment in the music.
  A power-off can lose up to an hour.
