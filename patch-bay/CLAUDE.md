# Patch Bay

Self-patching virtual modular for the M-VAVE FM-1, built on AMB-1 (charlesvestal/fm1-amb, MPL-2.0) and fm1-base
(`base/`, a submodule). Flashing goes through `../flasher` (its safety rules and flasher app); copy release builds
into its `firmware/patch-bay/` and add them to its `catalog.json` only after its chip check.

## Build (WSL Ubuntu)

One-time setup: `bash .wsl-setup.sh` inside WSL (JieLi toolchain in `~/.jieli`, three AC79 SDK files in
`~/fw-AC79_AIoT_SDK`, Pillow in `~/.venvs/fm1`). Keep LF line endings.

From Git Bash: `MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu -- bash -lc 'cd /mnt/d/Projects/FM1/patch-bay && PYTHON=~/.venvs/fm1/bin/python <cmd>' | tr -d '\0'`

- `./build.sh` dev package `app/build/patchbay.fwsc` (identity FM-1_9300)
- `./build.sh --release X.Y` release `app/build/patchbay-X.Y.fwsc`; needs `screenshot` in `app/fm1.toml`
- `./build.sh --host` the simulator; `sh tests/run_tests.sh` all tests; `sh tests/run_tests.sh bench` CPU timing
  per preset (seconds of CPU per second of audio, host)

Device build rules: no 64-bit division, no doubles or libm, no `strcmp` and friends in app code, no register access
outside `base/`. Budgets: RAM 98 KB, pool 344 KB, saved project under 3840 B.

## Where things are

- `app/dsp/patch.c` / `patch.h`: the instrument. Modules `M_*`, inputs `I_*`, outputs `O_*`, jack types
  (gate, CV, audio). Render runs in 32-sample control blocks; cables change through the command queue with
  crossfades (audio 12 ms fade, CV glides from the held value, pitch and gate switch at once).
  Rules: `pb_legal` (types match, no audio loops among VCF/VCA/DLY), `pb_musical` (something heard, pitch through
  the quantizer, noise only through a VCA). Presets: `PB_PRESET[]`, count `PB_NPRESETS`. Every preset must pass
  both rules; `tests/host/patch_test.c` checks that and fuzzes 2000 moves.
- `pb_auto_move` picks a move (re-route a source or destination, add, remove), only ones that keep the patch legal
  and musical. Level keeping: slow RMS per mixer input, slow AGC and a tanh limiter at the end.
- `app/bay.c`: the hands. Auto-patcher timing (`AUTO_S`, `MOVE_MS`, `HAND_MS` pause after the user touches
  something), the cursor, plug and unplug.
- `app/ui.c`: screen (4x4 panels, bezier cables, footer knobs), buttons, help cards, autosave, screen reader.
- `app/project.c`, `app/app.h`: `project_t` (magic "PBAY", format 1). Anything else in the store loads the
  first preset. New fields go in `rsv[]` and keep the size.
- `app/host.c`: simulator checks and commands used by `tests/scenarios/*.amb`.
- `app/fm1.toml`: identity. The USB name must keep "FM-1" so the flasher finds the unit.

USB audio follows MASTER; there is no separate USB level setting.
