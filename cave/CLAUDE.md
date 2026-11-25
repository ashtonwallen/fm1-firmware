# CAVE

Water dripping in a cavern, played as music, for the M-VAVE FM-1, on fm1-base. Built from FELT PIANO 1.0's tree
(itself from AMB-1 Drift: Charles Vestal's AMB-1, MPL-2.0). Own engine and screen; AMB-1's platform glue, knob strip,
help cards and screen reader kept. Flashing goes through `../flasher` (its own safety rules); releases are copied to
`flasher/firmware/cave/X.Y/`.

## Build (WSL Ubuntu)

From Git Bash: `MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu -- bash -lc 'cd /mnt/d/Projects/FM1/cave && PYTHON=~/.venvs/fm1/bin/python <cmd>' | tr -d '\0'`

- `./build.sh` dev package `app/build/cave.fwsc` (identity FM-1_6700)
- `./build.sh --release X.Y` release `app/build/cave-X.Y.fwsc` (FM-1_67XXYYZZ); needs `docs/img/screen-cave.png`
- `./build.sh --host` the simulator; `sh tests/run_tests.sh` all tests (run `./build.sh` first for the update path)
- Engine tests alone: `cc -O2 -ffp-contract=off -std=c99 -DEN_HOST -Iapp/dsp -Ibase/include -o /tmp/cave/ct tests/host/cave_test.c app/dsp/cave.c -lm`,
  then `/tmp/cave/ct` (tunings, worlds over a day, levels, events, cost), `/tmp/cave/ct dump CAVE MINUTES` (every
  note, the river's moves, events: read it as a musician), `/tmp/cave/ct hours CAVE HOURS` (the world over hours),
  `/tmp/cave/ct demos DIR` (one 20 s WAV per cave plus a 3 min listen from power-on)
- Engine cost: time `en_render` on the host against AMB-1 Drift's busiest piece (Low Tide, ~168 ns/frame). CAVE's
  busiest is about half that, dense with every event going.

Keep LF line endings. No doubles or libm in the engine (fastmath.h). No 64-bit division.

## Where things are

- Engine: `app/dsp/cave.c` / `cave.h`. Runs the world inside the render (audio interrupt).
  - Caves: `CAVES[]` (twelve): knob recipe, tonic, tuning set in cents (no semitones), the river's path of roots and
    its fifth, stone timbre, layout (counts, degree ranges), drip periods, harmony pace, bats' rate, echo time.
    Names follow `.claude-briefs/naming-rule.md` (no name used elsewhere in the repo).
  - World `en_world_t` (saved, ~170 bytes a cave): objects (stalagmites, pools: a degree of the set each), drip points
    (target, period, wetness threshold), wetness, harmony, event timers. `en_world_fresh`, `assign_drips` (first drip
    on a steady low note, the rest spread high and low), `world_second`/`world_minute` (growth: a stalagmite's note
    falls one degree; floods, stones, bats), `rearrange` (a flood's height).
  - Music: `drips` (each point on its period; chord tones of the river louder; splashes), `trickle` (curated falling
    phrases ending on a chord tone), `bats`, `stone`, `pebble` (keys snapped to the set). `chord_tone` judges against
    the river's root.
  - Sound: `strike` (stone: up to four two-pole resonators per timbre; water: a sine rising into pitch, a plop), the
    river (two crossfaded drone layers), ambience (rush, far untuned ticks: P_AMB alone scales it), cavern FDN, echo,
    leveller.
  - UI state: `en_world_get` (seqlock copy), the strike log `EN_LOG`, drip landings `en_drip_next`, event times.
- New parameters go at the end of the `P_*` enum, before `P_NPARAMS`; that changes the saved project, so bump
  `PROJ_FORMAT` in `app/app.h` and carry the old format over in `app/project.c`.
- UI: `app/ui.c`. Pages Cave/Space/Setup; the scene (`draw_cave`: back wall, ceiling, stalactites and drops,
  stalagmites, pools and ripples, sparks, river, bats, stone), header with the river's note, help cards
  `help_lines`, autosave (settings at most every 5 min, the world at most hourly, only at a lull), screen reader.
- Project: `app/project.c` (format 1, magic "CAVE"): settings per cave plus every cave's world; anything else in the
  store gives the defaults; a world that fails `en_world_valid` is found fresh.
- Host checks/commands: `app/host.c` (`ff MIN`, `flood`, `stone`, `bats`, `trickle`, `worldN`...); scenarios in
  `tests/scenarios/`; engine tests `tests/host/cave_test.c`.
- Words for the screen reader: `app/words.tsv` (the url line is a placeholder; nothing is published yet).
- USB name "CAVE FM-1" in `app/fm1.toml`: must keep "FM-1" so M-Flasher finds the unit.
