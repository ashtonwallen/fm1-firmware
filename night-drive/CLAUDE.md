# NIGHT DRIVE

Generative synthwave firmware for the M-VAVE FM-1, an app on fm1-base (`base/`, git submodule), started from
AMB-1 Drift (itself AMB-1 by Charles Vestal). Hand-off id `night-drive`. Flashing goes through `../flasher`
(its safety rules and flasher app); never flash from here.

## Build (WSL Ubuntu)

One-time setup: `bash .wsl-setup.sh` inside WSL (JieLi toolchain, three AC79 SDK files, Pillow venv). Keep LF line
endings.

From Git Bash: `MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu -- bash -lc 'cd /mnt/d/Projects/FM1/night-drive && PYTHON=~/.venvs/fm1/bin/python <cmd>'`

- `./build.sh` dev package `app/build/nightdrive.fwsc` (identity FM-1_9000)
- `./build.sh --release X.Y` release `app/build/nightdrive-X.Y.fwsc` (FM-1_90XXYYZZ); needs `docs/img/screen-drive.png`
- `./build.sh --host` simulator; `sh tests/run_tests.sh` everything; `sh tests/run_some.sh NAME...` a few scenarios
- Write multi-command WSL steps as script files; inline loops in `bash -lc` break on quoting.

## Where things are

- Engine `app/dsp/nd.c` / `nd.h` (built -O2, see `fm1.toml`). Parameters `P_*` (new ones go at the end); progressions
  `PROG[]`; presets `SND[]` (key, speed, pad/arp/bass/kick/hat/lead types, cbars, delay, reverb, gate, detune) and
  their knob recipes; `EN_NPRESETS` in `nd.h`. Section logic in the bar handler: INTRO 8 bars, DRIVE 8/16/32 by Drift,
  BREAK, BUILD 4, back to DRIVE. Key changes scale with Drift (none at 0), never more than 4 semitones from home.
- Mix: pads, bass, arp, lead, drums into ping-pong delay (dotted 8th) and Dattorro plate; gated room on the snare;
  limiter (T 0.62) then tanh. Peak at full MASTER about -5.7 dBFS. Re-check per-layer levels if you change gains.
- UI `app/ui.c`: pages Road and Setup, palettes `PAL[preset]`, the scene drawn in two bands (sun, stripes, ridges,
  grid lit on kick, road, car), header, knob strip, help cards, autosave, odometer, screen reader text.
- Saved project `app/app.h` / `app/project.c`: magic "NDRV", format 1, per-preset knob slots, odometer.
  Settings save 5 s after knobs rest, at most every 5 min, at a quiet moment; odometer at most hourly.
- Host hooks `app/host.c` (expect names, `storeamb`, `storeshort`, `odo`, `battery`).
- Screen reader words `app/words.tsv` -> `app/web/words/*.json` (keep every generated file).

## Tests

`tests/host/nd_test.c` (engine: every preset 120 s at full MASTER, Drift 100/0, key, sections, park; prints CPU)
and scenarios in `tests/scenarios/`: first, presets, controls, persist, foreign, reader, longrun (30 min simulated).
Look at screenshots in `app/build/scenarios/*/` after UI changes.
