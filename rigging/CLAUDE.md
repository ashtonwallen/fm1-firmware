# RIGGING

A tall ship crossing open ocean for the M-VAVE FM-1: every rope is a tuned string played by the wind; the wind's
angle to the ship picks the chord, so the ship's course moves the harmony. On fm1-base, built from FELT PIANO 1.0's
tree (which came from AMB-1 Drift: Charles Vestal's AMB-1, MPL-2.0). Own engine and screen; AMB-1's platform glue,
knob strip, help cards and screen reader kept. Flashing goes through `../flasher` (its own safety rules); releases
are copied to `flasher/firmware/rigging/X.Y/`.

## Build (WSL Ubuntu)

From Git Bash: `MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu -- bash -lc 'cd /mnt/d/Projects/FM1/rigging && PYTHON=~/.venvs/fm1/bin/python <cmd>' | tr -d '\0'`
(`$VAR`s in that command line are expanded before bash sees them: put loops in a script file.)

- `./build.sh` dev package `app/build/rigging.fwsc` (identity FM-1_6900)
- `./build.sh --release X.Y` release `app/build/rigging-X.Y.fwsc` (FM-1_69XXYYZZ); needs `docs/img/screen-ship.png`
- `./build.sh --host` the simulator; `sh tests/run_tests.sh` all tests (run `./build.sh` first for the update path)
- Engine tests alone: `sh tests/host/build_engine_test.sh /tmp/rigging/rt`, then `/tmp/rigging/rt` (checks, levels,
  a day at sea per preset for the music's rules, cost in a squall), `/tmp/rigging/rt dump PRESET MINUTES` (chords
  and their rope tunings, each rope as it starts, the weather a minute a line: read it as a musician would),
  `/tmp/rigging/rt demos DIR` (one 20 s WAV per preset plus a 3 min listen from power-on)
- Engine cost: the whole rig in a squall (all twelve ropes) is the worst case, ~1.0-1.1x AMB-1 Drift's busiest piece
  (Low Tide, ~168 ns/frame on this host)

Keep LF line endings. No doubles or libm in the engine (fastmath.h). No 64-bit division.

## Where things are

- Engine: `app/dsp/rig.c` / `rig.h`.
  - World (`world_step`, a block at a time): the helm (`new_course`: a point of sail or two away, now and then a
    tack or gybe; trims; HOLD), the wind (a slow mean per preset plus `wd.dev`, weather every 4-12 min, squalls
    `wd.sq`, calms), the swell (gusts on its crests: `gust_new`, each with its own angle, sweep and favoured
    ropes), near keys (`waters`, every 45-100 min), the watch's bells every half hour.
  - Harmony: `STYLES[].ch[]` is each preset's chain of five chords (cents above the tonic: root, two "triad" tones,
    colours) across the points of sail (`sector_of`, edges `SECT_EDGE`). `retarget` gives each rope the chord tone
    nearest its natural pitch (low ropes root or fifth, middle ones the triad; never a semitone or minor ninth against
    another, else the rope is muted for that chord). `harmony` marks the ropes that must move; they fade, and only
    when all are quiet does `harmony_apply` retune them (the common tones keep sounding). Never old and new at once.
  - Ropes (`RIG[]`, `ropes_step`, `render_ropes`): excitation by the wind's angle and strength (steady wind holds the
    low and middle ropes, gusts reach the high ones), the strongest `cap` sound (more as it blows; all in a squall,
    `wd.sqf`). Voice: two strands a hair apart, an octave and a twelfth (the harmonics rise with the wind: `harm`),
    a noise-driven resonator (Breath), Tone low-pass. Light-air presets' ropes a little up (`rope_mk`).
  - Hull hum (chord root, by the ship's way), bell (`bell_strike`), Ambience bed (`render_amb`: wind, sea wash, bow
    hiss, sail whoosh and flap, creaks), small FDN air, leveller.
  - Presets: `STYLES` (twelve: knobs, tonic, mode name, chain, near keys, squalls and calms an hour, minutes between
    courses, swell period, register, sky). Names follow `.claude-briefs/naming-rule.md` (no name used elsewhere).
- New parameters go at the end of the `P_*` enum, before `P_NPARAMS`; that changes the saved project, so bump
  `PROJ_FORMAT` in `app/app.h` and carry the old format over in `app/project.c`.
- UI: `app/ui.c`. Pages Sea/Rig/Setup, the scene (`draw_scene`: sky `SKY[]` by preset, swell, wind streaks, the ship
  posed on the swell via `ship_pt`, sails, masts, ropes `ROPE_P[]` lit by `en_rope_amp`, hull, compass, chord text),
  header (preset, force, HOLD), help cards `help_lines`, autosave (`autosave`: settings at most every 5 min, the
  voyage at most hourly, at a quiet moment), bells and squalls told (`events`), screen reader `speak` (chords in words:
  `sp_chord_words`).
- Project: `app/project.c` (format 1, magic "RIG1": settings, helm, lights and `en_world_t`); anything else in the
  store gives the defaults; a world that makes no sense starts a new voyage.
- Host checks/commands: `app/host.c` (`squall` starts one); scenarios in `tests/scenarios/`; engine tests
  `tests/host/rig_test.c`.
- Words for the screen reader: `app/words.tsv` (the url line is a placeholder; nothing is published yet).
- USB name "RIGGING FM-1" in `app/fm1.toml`: must keep "FM-1" so M-Flasher finds the unit.
