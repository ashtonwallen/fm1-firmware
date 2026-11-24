# MONASTERY

MONASTERY: self-playing firmware for the M-VAVE FM-1. Seven monks sing the eight daily offices in a stone chapel;
characters, events and a chronicle, kept across power-offs. Built from AMB-1 Drift (`../amb1-drift`,
remote `template`), itself AMB-1 by Charles Vestal (MPL-2.0). Flashing goes through `../flasher`
(its safety rules and flasher app); release builds are copied into its `firmware/monastery/<version>/`. Never flash
from here.

## Build (WSL Ubuntu)

One-time setup: `bash .wsl-setup.sh` inside WSL (JieLi toolchain, three AC79 SDK files, Pillow in `~/.venvs/fm1`).
The repo must keep LF line endings (`core.autocrlf false`, set, submodules too).

From Git Bash: `MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu -- bash -lc 'cd /mnt/d/Projects/FM1/monastery && PYTHON=~/.venvs/fm1/bin/python <cmd>'`
(pipe through `tr -d '\0'`)

- `./build.sh` dev package `app/build/monastery.fwsc` (identity FM-1_7900)
- `./build.sh --release X.Y` release `app/build/monastery-X.Y.fwsc` (FM-1_79XXYYZZ)
- `./build.sh --host` the simulator; `sh tests/run_tests.sh` all tests (scenarios in `tests/scenarios/*.amb`,
  screenshots and WAVs in `app/build/scenarios/`). The host prints each world event as `[day N Office h:mm] ...`.
- The firmware build refuses 64-bit division, doubles, libm; keep the world's maths in 32-bit ints or floats.

## Where things are

- `app/world.c`: the world. Brothers (`MONK_*` tables, fixed identities by index), offices (`OFFICE[]`: hour, mode,
  final, weight of the day, singers, pace, tolls), the chant (phrase builders `ph_build`, singers `pick`, habits in
  `ph_start`: novice slips, Ambrose's flat, the rivals' clash), daily turn `new_day` (learning, vows, illness,
  Bede's arrival, rivalry), events and the chronicle (`event`, `ev_text`), the voices to the engine (`voices_step`).
- `app/dsp/amb.c`: AMB-1's engine plus the choir (`en_choir`, `en_voice`: the pad's four formant voices driven one by
  one), the drone (`en_drone`, the bass) and the tower bell (`en_toll`). Bell loops and pulse are off by settings.
- `app/ui.c`: pages Chapel / Brothers / Space / Setup, the chapel drawing, autosave
  (`keep_due`, for flash wear: the world at most hourly, settings at most every 5 min, once on an empty battery; only
  while nobody sings), the screen reader.
- `app/app.h` `project_t` (magic "MON1", format 1): sound settings + `world_t` (one store object, under 3840 B).
- `app/host.c`: simulator checks (`office`, `day`, `singing`, `slipsN`...) and commands (`days N`, `office N`,
  `storeamb`).
- USB name "MONASTERY FM-1" in `app/fm1.toml`: must keep "FM-1" in it so M-Flasher finds the unit. `repo` and
  `install` there are placeholders (a release needs https addresses); no public repo yet.
- `web/` is AMB-1's browser app, not updated (its tests skip without emcc).
