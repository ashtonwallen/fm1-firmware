# AMB-1 Drift

version of AMB-1 (charlesvestal/fm1-amb, MPL-2.0) for the M-VAVE FM-1: Wander (glides from piece to piece on
a setting), new sounds (1.1: strings, lead, air, shimmer, a strings pad) and sixteen more pieces (24). Branch `drift`, from upstream tag v1.3.2 (remote `upstream`). Flashing goes through
`../flasher` (its safety rules and flasher app); copy release builds into its `firmware/amb-1-drift/`
and add them to its `catalog.json`.

## Build (WSL Ubuntu)

One-time setup: `bash .wsl-setup.sh` inside WSL (JieLi toolchain in `~/.jieli`, three AC79 SDK files in
`~/fw-AC79_AIoT_SDK`, Pillow in `~/.venvs/fm1`). The repo must keep LF line endings (`core.autocrlf false`, set).

From Git Bash: `MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu -- bash -lc 'cd /mnt/d/Projects/FM1/amb1-drift && PYTHON=~/.venvs/fm1/bin/python <cmd>'`

- `./build.sh` dev package `app/build/ambdrift.fwsc` (identity FM-1_7400)
- `./build.sh --release X.Y` release `app/build/ambdrift-X.Y.fwsc` (FM-1_74XXYYZZ)
- `./build.sh --host` the simulator; `sh tests/run_tests.sh` all tests (scenarios in `tests/scenarios/`, `wander.amb` is ours)

Sanity check of the toolchain: building upstream v1.3.2 with `--release 1.3.2` matched the official package byte for byte.

## Where things are

- Pieces: `app/dsp/amb.c` `PIECES[]` (one value per parameter, order in the comment above it); count in `amb.h` `EN_NPIECES`.
  The first sixteen end in `D11_OFF` (1.1's sounds off). New parameters go at the end of the `P_*` enum (scenarios use
  the numbers) and must default to off, so old pieces render identically.
- 1.1's sounds (`amb.c`, "AMB-1 Drift 1.1's sounds" and "Drift 1.1: strings, lead, air, shimmer"): STRINGS
  (Karplus-Strong, 4 voices, P_PLUCK..P_PLOCT, plucked from `strings_tick` on the grid), LEAD (one voice,
  P_LEAD..P_LDOCT, `lead_control`), AIR (filtered noise, P_TEXTURE/P_TEXLVL/P_TEXMOD, `air_control`), SHIMMER
  (an octave-up pitch shifter on the plate's output, P_SHIMMER), and pad colour 2 (Strings). They use their own
  random numbers (`rng2`) and do nothing at level 0: that is what keeps the old pieces bit-identical. Check it by
  rendering the old pieces before and after a change with the same seed (a small host harness linking `amb.c`).
- UI pages: Lead, Strings, Air are views 8..10 (after AMB-1's numbering), reached by ENV, ARP, FX pressed again;
  SELECT and the page dots follow `VIEW_ORDER` in `ui.c`.
- Wander: `app/ui.c` (the Wander section, the V_WANDER page, GLO toggles Setup/Wander); settings in `project_t`
  (`app/app.h`, format 6). Discrete parameters (`wd_discrete`: key, scale, octaves, pad colour, texture) switch
  halfway; the rest slide. Formats 3, 4 (AMB-1's) and 5 (Drift 1.0) are carried over in `app/project.c`; the new
  settings come from each piece's recipe. Tests: `wander.amb`, `drift.amb` (new pages, pieces, glides, format 5).
- USB name "AMB-1 Drift FM-1" in `app/fm1.toml`: must keep "FM-1" in it so M-Flasher finds the unit.
