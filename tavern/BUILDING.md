# Building TAVERN

TAVERN is an app on [fm1-base](https://github.com/charlesvestal/fm1-base) (`base/`, a git submodule, with
felucca-bsp inside it). Clone with `--recursive`, or after cloning:

```
git submodule update --init --recursive
```

`./build.sh` runs fm1-base's build (`base/tools/build.py`) on `app/`, whose settings are in `app/fm1.toml`. It
makes, in `app/build/`:

| File | What |
| --- | --- |
| `tavern.bin` | the firmware app |
| `loader/ota.bin` | the update loader (felucca-bsp's) |
| `tavern.fwsc` | the installable package (app + loader) |

## Prerequisites

- Linux x86-64 (or WSL Ubuntu): the JieLi Linux toolchain (clang 4.0.1 for pi32v2, from JieLi's package server;
  see fm1-base's README) and Python 3 with Pillow. `.wsl-setup.sh` sets all of it up in WSL.
- macOS: Docker or podman; the build runs the toolchain in a `linux/amd64` container (see fm1-base's README).
- The JieLi AC79 SDK (Apache-2.0). The package uses three of its files (`cpu/wl82/tools/uboot.boot`,
  `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`); none of it is part of this tree or of git.

  ```
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

## Build

```
./build.sh                    # app/build/tavern.fwsc, identity FM-1_8100
./build.sh --release X.Y      # app/build/tavern-X.Y.fwsc, identity FM-1_81XXYYZZ
./build.sh --host             # the simulator: app/build/host/tavern_host
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations (`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`),
`PYTHON` the Python that runs the build. The build also checks the image, RAM and pool sizes, that register access
stays in felucca-bsp, and that no double-precision code is linked.

## Tests

```
sh tests/run_tests.sh             # TAVERN's
sh base/tests/run_tests.sh        # fm1-base's own
```

TAVERN's: the band (`tests/host/amb_test.c`: tunes in their modes and AABB form, the voices, joining on the
repeat, levels); the update path against `app/build/tavern.fwsc`; and the whole app in the simulator
(`tests/scenarios/*.amb`: first sound, a whole night, drama over nights, the controls, persistence, another
firmware's store, the screen reader), with audio and screenshots in `app/build/scenarios/`.

`app/build/host/tavern_host SCRIPT OUTDIR` runs one script. The commands are listed at the top of
`base/host/plat_host.c`, and TAVERN's own checks and commands at the top of `app/host.c`.

## Install

Installing firmware is at your own risk. From the command line (needs `mido` and `python-rtmidi`):

```
python3 base/tools/fm1_install.py app/build/tavern.fwsc
```

Hold OCT- and OCT+ for 5 seconds for the update mode.

## Releases

`.github/workflows/release.yml` (from AMB-1) builds a tagged release on GitHub with the shared FM-1 builder,
[`zvenson/fm1-build` at `v1`](https://github.com/zvenson/fm1-build/tree/v1), and signs its build provenance.
TAVERN has no published repo yet: a local release build (`./build.sh --release X.Y`) is how it is tried.
