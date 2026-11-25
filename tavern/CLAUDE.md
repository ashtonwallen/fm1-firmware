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
  at what level, joining at phrase starts, the ending, sets: `en_next` queues the next tune, which follows
  straight on in its own key and tempo), AMB-1's plate reverb and tape echo. Tested by `tests/host/amb_test.c`.
  AMB-1 Drift 1.1's air (rain, wind, sea, fire) and shimmer are ported in: params `P_TEXTURE`..`P_SHIMMER` after
  `P_MODE`, not saved (`P_NSAVED` in `app.h` is the first of them).
- Presets (1.1): `app/project.c` `PRESET[]` (12, `NPRESET` in `app.h`, int16 values). `preset_par()` maps a knob's
  value to the engine's (room offsets) and gives the air and shimmer; `preset_kinds/kind_pct/mode/tempo/songs/inst`
  steer the world (`pick_kind`, `tune_key`, tempo, singers' chance, `band()`). PRESETS turns them (`preset_step` in
  `ui.c`: `project_apply`, `world_restyle`). Preset 0 keeps 1.0's sound (not its random sequence since 1.2).
- Ambience (1.2): `proj.amb[preset]`, 0..`AMB_STEPS` (25, 5 % a step, `AMB_DEF` 20 = the preset's own air), knob `K_AMB`
  on the Room page, named Rain/Wind/Sea/Fire by `preset_air()`; scales `P_TEXLVL` in `preset_par`.
- The tavern `app/world.c` / `world.h`: regulars (`REG[]`), landlord Hugh, the visitor, the night's clock and phases,
  events and the log, memorable nights, tune names, sets (`queue_next`, `segue`), trading parts
  (`trade_tick`). No moods, skill, drink or quarrels since 1.2: nobody plays badly on purpose; the engine has no wrong
  notes, sour tuning or sloppy timing either. Runs on the main loop and sends the engine `en_tune`/`en_inst`.
- Screen `app/ui.c`: pages Tavern, Regulars, Nights, Mix, Room, Setup; autosave (`keep_due`, for flash wear: the
  world at most hourly, settings at most every 5 min, once on an empty battery; only between tunes once the room is
  quiet).
- Saved state `project_t` in `app/app.h` (magic "TAV1", format 3, must stay at most 3840 bytes: a typedef checks it).
  `par[P_NSAVED]` keeps 1.0's layout; the preset is 1.0's spare byte; `amb[]` took 12 of `rsv`. Formats 1 and 2 load (1 on preset 0),
  ambience at default, and `world_migrate()` clears 1.1's drama fields (`x_` in `world.h`, layout kept), mending,
  its memories and the log. Anything else
  in the store (another firmware's project) loads as defaults (`app/project.c`).
- Host checks and commands (`expect present`, `preset`, `format`, `effN` the engine's value under the preset,
  `timewarp N`, `hold N`, `endtune`, `storeamb`, `storev1`, `storev2`, `badev` (events in the words of 1.1's
  mishaps), `offkey`/`offchord` (every note checked in `host_note`), `segues`, `trades`, `amb`, `notelog FILE`: what
  the band plays, a bar a line...) at the top of `app/host.c`. `session.amb`: two nights, no mistakes, the note log;
  `ambience.amb`: every preset's ambience off, up, kept, saved. `tests/scenarios/presets.amb`:
  every preset plays, peaks, the regulars and night kept, format 1.
- USB name "TAVERN FM-1" in `app/fm1.toml`: keep "FM-1" in it so M-Flasher finds the unit. Repo and install URLs
  there are placeholders (nothing is published).
