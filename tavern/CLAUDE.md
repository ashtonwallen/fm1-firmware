# TAVERN

TAVERN: a folk-session virtual pet for the M-VAVE FM-1, forked from AMB1Drift (AMB-1 by Charles Vestal, MPL-2.0) on
fm1-base. Branch `main`, remote `template` (../amb1-drift). Flashing goes through
`../flasher` (its safety rules and flasher app); release builds are copied into its
`firmware/tavern/X.Y/`. Never flash or talk to the FM-1 from this repo.

## Build (WSL Ubuntu)

One-time setup: `bash .wsl-setup.sh` inside WSL (JieLi toolchain in `~/.jieli`, three AC79 SDK files in
`~/fw-AC79_AIoT_SDK`, Pillow in `~/.venvs/fm1`). Keep LF line endings (`core.autocrlf false`).

From Git Bash: `MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu -- bash -lc 'cd /mnt/d/Projects/FM1/tavern && PYTHON=~/.venvs/fm1/bin/python <cmd>'`

- `./build.sh` dev package `app/build/tavern.fwsc` (identity FM-1_8100)
- `./build.sh --release X.Y` release `app/build/tavern-X.Y.fwsc` (FM-1_81XXYYZZ)
- `./build.sh --host` the simulator; `sh tests/run_tests.sh` all tests (no emcc in WSL: the emu test skips)

## Where things are

- Engine `app/dsp/amb.c` / `amb.h` (name kept from AMB-1): tune generator (`en_tune_make`, modes, AABB, second
  endings), the instruments (fiddle, whistle, voice, harp/lute FM plucks, box, bass, bodhran), the session (who plays
  at what level, joining on bar lines, the ending), AMB-1's plate reverb and tape echo. Tested by `tests/host/amb_test.c`.
- The tavern `app/world.c` / `world.h`: regulars (`REG[]`), landlord Hugh, the visitor, the night's clock and phases,
  events and the log, memorable nights, tune names. Runs on the main loop and sends the engine `en_tune`/`en_inst`.
- Screen `app/ui.c`: pages Tavern, Regulars, Nights, Mix, Room, Setup; autosave (`keep_due`, for flash wear: the
  world at most hourly, settings at most every 5 min, once on an empty battery; only between tunes once the room is
  quiet).
- Saved state `project_t` in `app/app.h` (magic "TAV1", format 1, must stay at most 3840 bytes: a typedef checks it).
  Anything else in the store (another firmware's project) loads as defaults (`app/project.c`).
- Host checks and commands (`expect present`, `timewarp N`, `hold N`, `endtune`, `storeamb`...) at the top of `app/host.c`.
- USB name "TAVERN FM-1" in `app/fm1.toml`: keep "FM-1" in it so M-Flasher finds the unit. Repo and install URLs
  there are placeholders (nothing is published).
