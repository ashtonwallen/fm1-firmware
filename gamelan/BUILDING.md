# Building GAMELAN

GAMELAN is an app on [fm1-base](https://github.com/charlesvestal/fm1-base) (`base/`, a git submodule, with
felucca-bsp inside it), built the way AMB-1 is. Clone with `--recursive`, or after cloning:

```
git submodule update --init --recursive
```

`./build.sh` runs fm1-base's build (`base/tools/build.py`) on `app/`, whose settings are in `app/fm1.toml`. It
makes, in `app/build/`:

| File | What |
| --- | --- |
| `gamelan.bin` | the firmware app |
| `loader/ota.bin` | the update loader (felucca-bsp's) |
| `gamelan.fwsc` | the installable package (app + loader) |

## Prerequisites

- Python 3 with Pillow (with libraqm, for the fonts).
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2, from JieLi's package server): see fm1-base's README. It is
  Linux x86-64 only: on Linux or WSL it runs natively; on macOS the build runs it in a `linux/amd64`
  `debian:bookworm-slim` container (Docker Desktop, or podman with a `docker` script).
- The JieLi AC79 SDK (Apache-2.0). The package uses three of its files (`cpu/wl82/tools/uboot.boot`,
  `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`); none of it is part of this tree or of git.

  ```
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

`.wsl-setup.sh` sets all of it up inside WSL Ubuntu.

## Build

```
./build.sh                    # app/build/gamelan.fwsc, identity FM-1_9200
./build.sh --release X.Y      # app/build/gamelan-X.Y.fwsc, identity FM-1_92XXYYZZ
./build.sh --host             # the simulator: app/build/host/gamelan_host
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations (`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`),
`PYTHON` the Python that runs the build. The build also checks the image, RAM and pool sizes, that register access
stays in felucca-bsp, and that no double-precision code is linked.

## Tests

```
sh tests/run_tests.sh             # GAMELAN's
sh base/tests/run_tests.sh        # fm1-base's own
```

GAMELAN's: the instrument (`tests/host/gam_test.c`: tunings, the balungan made, every preset rendered, a 40-minute
run, hold, irama, the keys); the update path against `app/build/gamelan.fwsc`; and the whole app in the simulator
(`tests/scenarios/*.amb`: sound from power-on, every preset, the controls, persistence and another firmware's data,
the screen reader, the battery, a 65-minute peak check), with audio and screenshots in `app/build/scenarios/`.

`app/build/host/gamelan_host SCRIPT OUTDIR` runs one script. The commands are listed at the top of
`base/host/plat_host.c`, and GAMELAN's own checks and commands at the top of `app/host.c`.

`GAM_BENCH=1 app/build/tests/gam_test` prints the engine's time per second of audio for each preset.

## Install

From the command line (needs `pip3 install mido python-rtmidi`):

```
python3 base/tools/fm1_install.py app/build/gamelan.fwsc
```

Installing firmware is at your own risk. Hold OCT− and OCT+ for 5 seconds for the update mode. If the FM-1 no
longer starts but reaches the chip's update mode (4C4A:8057 on USB), `base/bsp/tools/fm1_rescue.sh` puts stock
firmware back from a Mac.
