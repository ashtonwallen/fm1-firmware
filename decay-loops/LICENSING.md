# DECAY LOOPS licensing

DECAY LOOPS is built on AMB-1 by Charles Vestal: its app shell (the panel, the knob strip, the help cards, the
screen reader, saving, the host checks) and its harmony and Dattorro plate reverb carry on here, with a new engine
(`app/dsp/decay.c`: the phrase synth, the tape and its wear) and a new screen by ashtonwallen. Same licence: the
**Mozilla Public License 2.0**, file by file. New files (`app/dsp/decay.c`, `app/dsp/decay.h`, `tests/host/dl_test.c`,
`tests/scenarios/`) and changed ones (`app/ui.c`, `app/project.c`, `app/app.h`, `app/app.c`, `app/host.c`) are
MPL-2.0, with Charles Vestal's copyright kept where his code remains. The platform is fm1-base and felucca-bsp, from
Felucca by Leo Kuroshita (H�gelton Instruments). The idea comes from William Basinski's The Disintegration Loops; no
material from it is used. AMB-1's own licensing notes follow, with "AMB-1" standing for the code it gave.

# AMB-1 licensing

AMB-1 is free software under the **Mozilla Public License 2.0** (`MPL-2.0`, full text in `LICENSE`), file by file.
If you change one of its files and share the result, share that file's source under the MPL. It is built on
[fm1-base](https://github.com/charlesvestal/fm1-base) and [felucca-bsp](https://github.com/hugelton/felucca-bsp)
(`base/`, a git submodule), both MPL-2.0.

Until 0.9, AMB-1 was GPL-3.0-only, built on Felucca's platform layer. That code is gone from this tree: the platform
is fm1-base's now, and AMB-1's own code (the engine, the UI, the pieces) is relicensed by its author. AMB-1 0.9 and
earlier stay under the GPL.

## Where the code comes from

| What | Origin | Licence |
| --- | --- | --- |
| The platform: `base/` (boot, USB and USB audio, the TRS jack, storage, the update path, screen, panel, the screen reader, the host simulator and the browser emulator, build and install tools) | [fm1-base](https://github.com/charlesvestal/fm1-base) and [felucca-bsp](https://github.com/hugelton/felucca-bsp), from [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (@kurogedelic), Hügelton Instruments, under MPL-2.0 with his permission ([felucca-bsp#4](https://github.com/hugelton/felucca-bsp/issues/4)) | MPL-2.0 |
| `app/` (`app.c`, `app.h`, `ui.c`, `project.c`, `host.c`, and AMB-1's `dsp/amb.c`, whose harmony and Dattorro plate (from FoMni-1) live on in `dsp/decay.c`) | AMB-1, Charles Vestal | MPL-2.0 |
| The idea of a browser emulator for FM-1 firmware (fm1-base's emulator) | [Groove OS](https://www.groove-os.com/) by Peter Gombos, whose site runs its firmware in the browser; no code from it | credit only |

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Fonts: Barlow Semi Condensed (The Barlow Project Authors), Terminus (Dimitar Toshkov Zhekov) | SIL OFL 1.1 | `base/assets/fonts/` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments. "M-VAVE" and "FM-1" are trademarks of their
respective owners, used here only to say what hardware DECAY LOOPS runs on. It is independent firmware; it is not
affiliated with, endorsed by or supported by any of them.

## Radio

DECAY LOOPS never enables the Bluetooth / Wi-Fi radio of the hardware.
