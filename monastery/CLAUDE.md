# MONASTERY

MONASTERY: self-playing firmware for the M-VAVE FM-1. Seven monks sing the eight daily offices in a stone chapel;
characters, events and a chronicle, kept across power-offs. Since 1.2 the characters only shape the arrangement
(who intones, leads, trades verses, sings solo, holds the ison or the organum, who is in choir); nobody sings a wrong
note, early, late, flat or against another. Keep it that way: the owner listens to it as music. Built from AMB-1 Drift (`../amb1-drift`,
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

- `app/world.c`: the world. Brothers (`MONK_*` tables, fixed identities by index; `MONK_CENTS` a small chorus, at
  most 3 cents), offices (`OFFICE[]`: hour, mode, final, weight of the day, singers, pace, tolls, the low part
  `LOW_*`, choir size), the chant (phrase builders `ph_build`, who is in the stalls `choir_pick`, singers `pick`,
  how a psalm's verses go `psalm_style`: sides / cantors trading / soloist and choir, the parts in `ph_start`:
  line, organum `org_deg`, ison), verse overlap only where the new verse is consonant with the held final
  (`chant_step`), daily turn `new_day` (the novices' `part` grows: choruses, verses at 30, solos at 60, vows; Bede's
  arrival), events and the chronicle (`event`, `ev_text`; 1.1's event numbers kept, its mistake kinds `EV_GONE*`
  never sung and dropped from old chronicles by `world_migrate`), the voices to the engine (`voices_step`; in the
  host `host_listen` counts notes out of mode, out of tune or clashing, and `notelog 1` prints every note).
- `app/dsp/amb.c`: AMB-1's engine plus the choir (`en_choir`, `en_voice`: the pad's four formant voices driven one by
  one), the drone (`en_drone`, the bass) and the tower bell (`en_toll`). Bell loops and pulse are off by settings.
  AMB-1 Drift 1.1's sounds are ported in (strings, lead, air, shimmer: params `P_PLUCK`..`P_SHIMMER` at the end).
  They play from the engine's scale: `world_key()` sets the root and pool to the office's final and mode.
- Presets (1.1): `project.c` `PRESET[]` (12, `NPRESET` in `app.h`). `preset_par()` maps a knob's value to the
  engine's (levels %, space offsets) and gives Drift 1.1's params; `preset_final/mode/pace/formant/vib` move the
  chant (world.c `of_final`, `of_mode`). PRESETS turns them (`preset_step` in `ui.c`, then `world_preset()`).
  Abbey Chapel (0) changes nothing. The world is never touched.
- `app/ui.c`: pages Chapel / Brothers / Space / Setup, the chapel drawing, autosave
  (`keep_due`, for flash wear: the world at most hourly, settings at most every 5 min, once on an empty battery; only
  while nobody sings), the screen reader.
- The Ambience (1.2): Space page knob 4 (`K_AMB` in `ui.c`), `proj.amb[NPRESET]`, 0..24 steps of 5 % of the
  preset's air (`preset_par` P_TEXLVL), 100 % default, per preset; "None" on presets without air. The Pace knob it
  replaced lives on OCT-/OCT+. Help cards on held buttons (`help_lines`, ported from AMB-1 Drift).
- `app/app.h` `project_t` (magic "MON1", format 3): sound settings (`par[P_NSAVED]`, AMB-1's 28 only, so the layout
  is 1.0's) + the preset (1.0's spare byte) + `world_t` (one store object, under 3840 B) + the Ambience (in 1.1's
  spare bytes). Formats 1 and 2 have the same layout and are migrated (`project_load`, `world_migrate`): 1.1's
  monk fields (health, mood, illness, slips, naps, flats, clashes) and the rivalry are now reserved or reused.
- `app/host.c`: simulator checks (`office`, `day`, `singing`, `choir`, `partN`, `solosN`, `amb`, `preset`,
  `format`, `effN` the engine's value under the preset, `offmode` `offtune` `clash` `gone` `chrongone`...) and
  commands (`days N`, `office N`, `notelog 1`, `storeamb`, `storev1`, `storev2`). Scenarios: `presets.amb` every
  preset sings, peaks; `musician.amb` a day under every preset, no wrong note; `ambience.amb` the control on every
  preset; `migrate.amb` a real 1.1 flash (`tests/fixtures/flash-1.1.bin.gz`) and a synthetic one load.
- USB name "MONASTERY FM-1" in `app/fm1.toml`: must keep "FM-1" in it so M-Flasher finds the unit. `repo` and
  `install` there are placeholders (a release needs https addresses); no public repo yet.
- `web/` is AMB-1's browser app, not updated (its tests skip without emcc).
