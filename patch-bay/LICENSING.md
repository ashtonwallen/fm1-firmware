# Patch Bay licensing

Patch Bay (ashtonwallen) is built on AMB-1 by Charles Vestal and keeps its licence, the **Mozilla Public License
2.0** (`MPL-2.0`, full text in `LICENSE`), file by file. Its own parts are the instrument (`app/dsp/patch.c`,
`patch.h`), the patch's hands (`app/bay.c`), the rewritten `app/ui.c`, `app/project.c`, `app/host.c`, `app/app.c`,
`app/app.h` and the tests; every file says so in its header. If you change one of these files and share the result,
share that file's source under the MPL.

## Where the code comes from

| What | Origin | Licence |
| --- | --- | --- |
| The platform: `base/` (boot, USB and USB audio, the TRS jack, storage, the update path, screen, panel, the screen reader, the host simulator, build and install tools) | [fm1-base](https://github.com/charlesvestal/fm1-base) and [felucca-bsp](https://github.com/hugelton/felucca-bsp), from [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (@kurogedelic), Hügelton Instruments, under MPL-2.0 with his permission ([felucca-bsp#4](https://github.com/hugelton/felucca-bsp/issues/4)) | MPL-2.0 |
| The app's frame: the command queue, the Dattorro plate reverb (from FoMni-1), the message, knob, help-card, autosave and screen-reader machinery, the anti-aliased drawing (from X0X), the build script and test runner | [AMB-1](https://github.com/charlesvestal/fm1-amb), Charles Vestal | MPL-2.0 |
| The modules, the patch rules, the auto-patcher, the screen, the presets | Patch Bay, ashtonwallen | MPL-2.0 |

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Fonts: Barlow Semi Condensed (The Barlow Project Authors), Terminus (Dimitar Toshkov Zhekov) | SIL OFL 1.1 | `base/assets/fonts/` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments. "M-VAVE" and "FM-1" are trademarks of their
respective owners, used here only to say what hardware Patch Bay runs on. Patch Bay is independent firmware; it is
not affiliated with, endorsed by or supported by any of them.

## Radio

Patch Bay never enables the Bluetooth / Wi-Fi radio of the hardware.
