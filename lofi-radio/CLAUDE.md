# LOFI RADIO

Endless generated lo-fi hip-hop for the M-VAVE FM-1, on fm1-base, built from AMB-1 Drift's tree (Charles Vestal's
AMB-1, MPL-2.0). New engine and UI; AMB-1's platform glue, knob strip, help cards and screen reader kept. Flashing
goes through `../flasher` (its own safety rules); releases are copied to `flasher/firmware/lofi-radio/X.Y/`.

## Build (WSL Ubuntu)

From Git Bash: `MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu -- bash -lc 'cd /mnt/d/Projects/FM1/lofi-radio && PYTHON=~/.venvs/fm1/bin/python <cmd>' | tr -d '\0'`

- `./build.sh` dev package `app/build/lofiradio.fwsc` (identity FM-1_8900)
- `./build.sh --release X.Y` release `app/build/lofiradio-X.Y.fwsc` (FM-1_89XXYYZZ); needs `docs/img/screen-radio.png`
- `./build.sh --host` the simulator; `sh tests/run_tests.sh` all tests (run `./build.sh` first for the update path)
- Engine cost: time `en_render` on the host against AMB-1 Drift's busiest piece (Low Tide, ~168 ns/frame); LOFI
  RADIO 1.0's busiest style measured ~170 ns/frame on the same host. For 1.1 vs 1.0, build both engines (1.0 is git
  tag `lofi-radio-1.0`, `git show` its `app/dsp/lofi.c`/`lofi.h`) into one bench and run them back to back on the
  same machine, min of a few runs: single runs of `lofi_test` swing by 20 %

Keep LF line endings. No doubles or libm in the engine (fastmath.h). No 64-bit division.

## Where things are

- Engine: `app/dsp/lofi.c` / `lofi.h`. Parameters (`PARAMS`, `P_*`), styles (`STYLES`: recipe, keys timbre, kit,
  melody voice, laid-back time, groove, extra kit `KT_*`, guests `IN_*`), chord qualities (16 at most: the screen
  hashes the quality in 4 bits) and rootless voicings, 40 progressions (`PROGS`, mood and major/minor; recent ones
  avoided), grooves, comping and bass patterns, track plan (intro, A, B, breakdown, outro, runout), titles
  (`en_title`), voices (FM electric piano, bass and sine sub, drums plus brushes/rim click/shaker, melody: muted lead,
  vibraphone or muted trumpet; nylon guitar (Karplus-Strong, buffers in the pool), chopped formant vocal),
  crackle/rain, wobble, echo, FDN reverb, tone, leveller. Commands go main to render through a queue (`en_set`,
  `en_style`, `en_skip`...); UI reads `en_*` state.
- 1.1's live parts (all from the track's `trng`, so a seed still gives the same track): harmony is played from a
  timeline a cycle at a time (`tl_build`: `vary` substitutions, passing chords, ii-Vs; plain first cycle, B sections
  vary most), counted in harmony beats `tk.hb` so a repeat runs on; melody in 8-bar groups (`mel_group`: plan of
  question/answer/rest, the motif developed by `mot_dev` from the third group on), each phrase's last note onto a
  chord tone (`mel_rest_on`); drums: fills where a section ends, thin/drop bars, breakdown out/half-time/kick+hat
  (`tk.bd_mode`); who comps `comp_inst` (keys, guitar in intro/A, vocal in the breakdown and the B's second half),
  sub in A and breakdown, trumpet for the whole track.
- Host-only (`EN_HOST`) instrumentation: `en_fired` (events by type) and `en_part` (each part's level, with
  `en_meas` set); `lofi_test` prints a part-level table and checks every guest sounds and none is louder than the
  part it stands in for.
- New parameters go at the end of the `P_*` enum, before `P_NPARAMS`; that changes the saved project, so bump
  `PROJ_FORMAT` in `app/app.h` and carry the old format over in `app/project.c`.
- UI: `app/ui.c`. Pages Radio/Mix/Room, turntable (`REC_*`, precomputed record tables in the pool), tonearm, VU,
  help cards `help_lines`, autosave (knobs rested 10 s, settings at most every 5 min, track position at most
  hourly, only while paused or in a runout), screen reader `speak`.
- Project: `app/project.c` (format 2, magic "LOF1"). Format 1 (1.0) has the same layout and loads as it is
  (`migrate.amb`, host command `storev1`); anything else in the store gives the defaults.
- Host checks/commands: `app/host.c`; scenarios in `tests/scenarios/`; engine tests `tests/host/lofi_test.c`.
- Words for the screen reader: `app/words.tsv` (the url line is a placeholder; nothing is published yet).
- USB name "LOFI RADIO FM-1" in `app/fm1.toml`: must keep "FM-1" so M-Flasher finds the unit.
