# Building MINIMAL MACHINE

MINIMAL MACHINE is an app on [fm1-base](https://github.com/charlesvestal/fm1-base) (`base/`, a git submodule, with
felucca-bsp inside it). After cloning: `git submodule update --init --recursive`.

`./build.sh` runs fm1-base's build (`base/tools/build.py`) on `app/`, whose settings are in `app/fm1.toml`.

## Prerequisites

- Linux x86-64 (or WSL), Python 3 with Pillow.
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2): see fm1-base's README. Default `~/.jieli/toolchain`
  (`JIELI_TOOLCHAIN`).
- The JieLi AC79 SDK (Apache-2.0), for three files the package uses. Default `~/fw-AC79_AIoT_SDK` (`AC79_SDK`):

  ```
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

`.wsl-setup.sh` sets all of this up once inside WSL.

## Build

```
./build.sh                    # app/build/minimal.fwsc, identity FM-1_9100
./build.sh --release X.Y      # app/build/minimal-X.Y.fwsc, identity FM-1_91XXYYZZ
./build.sh --host             # the simulator: app/build/host/minimal_host
```

The build checks image, RAM and pool sizes, that register access stays in felucca-bsp, and that no
double-precision code is linked.

## Tests

```
sh tests/run_tests.sh             # MINIMAL MACHINE's
sh base/tests/run_tests.sh        # fm1-base's own
```

The engine (`tests/host/mm_test.c`: every preset makes sound and stays under its ceiling, mutations, Hold, Density,
the evolved loops' round trip); the update path against `app/build/minimal.fwsc`; and the whole app in the
simulator (`tests/scenarios/*.amb`), with audio and screenshots in `app/build/scenarios/`. Script commands are at
the top of `base/host/plat_host.c`; the app's own checks and commands at the top of `app/host.c`.

## Install

```
python3 base/tools/fm1_install.py app/build/minimal-X.Y.fwsc
```

At your own risk. Hold OCT- and OCT+ for 5 seconds for the update mode.
