# DEEP HOUSE DECKS

Endless generated deep house DJ set for the M-VAVE FM-1, on fm1-base, built from LOFI RADIO's tree (itself from
Charles Vestal's AMB-1, MPL-2.0). New engine and UI; AMB-1's platform glue, knob strip, help cards and screen reader
kept. Flashing goes through `../flasher` (its own safety rules); releases are copied to
`flasher/firmware/deep-house/X.Y/`.

## Build (WSL Ubuntu)

From Git Bash: `MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu -- bash -lc 'cd /mnt/d/Projects/FM1/deep-house && PYTHON=~/.venvs/fm1/bin/python <cmd>' | tr -d '\0'`
(a `$var` inside that quoted command gets lost: put loops in a script file and run that).

- `./build.sh` dev package `app/build/deephouse.fwsc` (identity FM-1_9500)
- `./build.sh --release X.Y` release `app/build/deephouse-X.Y.fwsc` (FM-1_95XXYYZZ); needs `docs/img/screen-decks.png`
- `./build.sh --host` the simulator; `sh tests/run_tests.sh` all tests (run `./build.sh` first for the update path)
- Engine tests alone: `tests/host/house_test.c` (see its header): every set 30 s + 15 min (peaks, blends, key
  moves), extremes, hold, stop/resume, cost. Extra modes: `dump SET SEED BARS` prints chords, bass, lead and drums
  bar by bar (check the music this way); `wav DIR` renders a clip per set; `time` times the busiest set.
- Engine cost: time `en_render` on the host against AMB-1 Drift's busiest piece (Low Tide, ~168 ns/frame) with
  thread CPU time, alternating runs; the host is noisy, so compare minima of several runs.

Keep LF line endings. No doubles or libm in the engine (fastmath.h). No 64-bit division.

## Where things are

- Engine: `app/dsp/house.c` / `house.h`.
  - Parameters `PARAMS` / `P_*`: the first ten are a set's own, Filter and Mix the player's.
  - Chord qualities and voicings (`Q_*`), curated vamps `VAMPS` (mood-tagged), comping `COMPS`, bass patterns
    `BPATS`, hats `HATS`, percussion `PERC`, lead hooks `HOOKS` (chord tones only), vocal chops `VOXES`.
  - Sets `STYLES`: recipe, keys timbre, pad, kit, percussion, bass voice, leads, vamps/comps/bpats/hats masks.
  - Record plan: intro (drums), groove, a body of 16-bar phrases from templates (G groove, F full, B breakdown +
    build), outro, tail. The DJ (`dj_bar`, `dj_hands`): cue the next record 24 bars before the out point, blend of
    16/32/64 bars, low-EQ swap at the half, filter sweeps, crossfader; only one deck plays harmony at a time.
  - Titles `en_title` (word lists checked against the other firmwares: no repeats), mix tags `en_mix_tag`.
  - Commands go main to render through a queue (`en_set`, `en_style`, `en_mix`, `en_hold`...); the UI reads
    `en_deck[]` and the other `en_*` state.
- New parameters go at the end of the `P_*` enum, before `P_NPARAMS`; that changes the saved project, so bump
  `PROJ_FORMAT` in `app/app.h` and carry the old format over in `app/project.c`.
- UI: `app/ui.c`. Pages Set/Groove/Levels, platters (`PL_*`, precomputed radius/angle tables in the pool),
  tonearms, mixer (`draw_mixer`), deck info, help cards `help_lines`, autosave (knobs rested 10 s, settings at most
  every 5 min, the record at most hourly, only while stopped or in a breakdown with no blend), screen reader `speak`.
- Project: `app/project.c` (format 1, magic "DHS1"); anything else in the store gives the defaults.
- Host checks/commands: `app/host.c`; scenarios in `tests/scenarios/`.
- Words for the screen reader: `app/words.tsv`.
- USB name "DEEP HOUSE FM-1" in `app/fm1.toml`: must keep "FM-1" so M-Flasher finds the unit.
- Names (sets, title words): none may appear in another firmware here (`../.claude-briefs/naming-rule.md`).
