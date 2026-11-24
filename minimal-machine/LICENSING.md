# MINIMAL MACHINE licensing

MINIMAL MACHINE is free software under the **Mozilla Public License 2.0** (`MPL-2.0`, full text in `LICENSE`), file
by file. If you change one of its files and share the result, share that file's source under the MPL.

## Where the code comes from

| What | Origin | Licence |
| --- | --- | --- |
| The platform: `base/` (boot, USB and USB audio, storage, the update path, screen, panel, the screen reader, the host simulator, build and install tools) | [fm1-base](https://github.com/charlesvestal/fm1-base) and [felucca-bsp](https://github.com/hugelton/felucca-bsp), from [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (@kurogedelic), Hügelton Instruments, under MPL-2.0 with his permission | MPL-2.0 |
| `app/app.c`, `app/app.h`, `app/project.c`, `app/ui.c`, `app/host.c`, `build.sh`, `tests/run_tests.sh`: the app skeleton, drawing helpers, knob strip, help cards and screen reader | [AMB-1](https://github.com/charlesvestal/fm1-amb), Charles Vestal, by way of AMB-1 Drift; reshaped for MINIMAL MACHINE by ashtonwallen | MPL-2.0 |
| `app/dsp/mm.c`, `app/dsp/mm.h` (the engine; its command queue follows AMB-1's), `tests/host/mm_test.c`, `tests/scenarios/` | ashtonwallen | MPL-2.0 |

The idea follows DUB-1 (Charles Vestal's dub techno firmware); none of its code is used here.

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Fonts: Barlow Semi Condensed (The Barlow Project Authors), Terminus (Dimitar Toshkov Zhekov) | SIL OFL 1.1 | `base/assets/fonts/` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## Trademarks

"M-VAVE" and "FM-1" are trademarks of their respective owners, used here only to say what hardware this runs on.
MINIMAL MACHINE is independent firmware; it is not affiliated with, endorsed by or supported by any of them.

## Radio

MINIMAL MACHINE never enables the Bluetooth / Wi-Fi radio of the hardware.
