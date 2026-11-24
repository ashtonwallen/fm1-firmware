# NIGHT DRIVE licensing

NIGHT DRIVE is free software under the **Mozilla Public License 2.0** (`MPL-2.0`, full text in `LICENSE`), file by
file. If you change one of its files and share the result, share that file's source under the MPL. It is built on
AMB-1 by Charles Vestal and on [fm1-base](https://github.com/charlesvestal/fm1-base) and
[felucca-bsp](https://github.com/hugelton/felucca-bsp) (`base/`, a git submodule), all MPL-2.0.

## Where the code comes from

| What | Origin | Licence |
| --- | --- | --- |
| The platform: `base/` (boot, USB and USB audio, the TRS jack, storage, the update path, screen, panel, the screen reader, the host simulator and the browser emulator, build and install tools) | [fm1-base](https://github.com/charlesvestal/fm1-base) and [felucca-bsp](https://github.com/hugelton/felucca-bsp), from [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (@kurogedelic), Hügelton Instruments, under MPL-2.0 with his permission ([felucca-bsp#4](https://github.com/hugelton/felucca-bsp/issues/4)) | MPL-2.0 |
| App structure (`app.c`, `app.h`, `project.c`, `host.c`, `ui.c`'s knob strip, help cards and screen reader patterns), `build.sh`, `tests/run_tests.sh`, `tests/host/emu_test.mjs` | [AMB-1](https://github.com/charlesvestal/fm1-amb), Charles Vestal, changed for NIGHT DRIVE | MPL-2.0 |
| `app/dsp/nd.c`, `nd.h` (the synthwave engine; its plate reverb after Jon Dattorro, as in AMB-1), the scene and pages in `ui.c`, `tests/host/nd_test.c`, `tests/scenarios/` | NIGHT DRIVE, ashtonwallen | MPL-2.0 |

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Fonts: Barlow Semi Condensed (The Barlow Project Authors), Terminus (Dimitar Toshkov Zhekov) | SIL OFL 1.1 | `base/assets/fonts/` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments. "M-VAVE" and "FM-1" are trademarks of their
respective owners, used here only to say what hardware NIGHT DRIVE runs on. NIGHT DRIVE is independent firmware; it
is not affiliated with, endorsed by or supported by any of them.

## Radio

NIGHT DRIVE never enables the Bluetooth / Wi-Fi radio of the hardware.
