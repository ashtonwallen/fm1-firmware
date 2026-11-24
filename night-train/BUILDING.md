# Building NIGHT TRAIN

NIGHT TRAIN is an app on [fm1-base](https://github.com/charlesvestal/fm1-base) (`base/`, a git submodule, with
felucca-bsp inside it). Clone with `--recursive`, or `git submodule update --init --recursive`.

`./build.sh` runs fm1-base's build (`base/tools/build.py`) on `app/` (settings in `app/fm1.toml`). In `app/build/`:

| File | What |
| --- | --- |
| `nighttrain.bin` | the firmware app |
| `loader/ota.bin` | the update loader (felucca-bsp's) |
| `nighttrain.fwsc` | the installable package (app + loader) |

Prerequisites: Python 3 with Pillow; the JieLi Linux toolchain (Linux x86-64; WSL works); three files of the JieLi
AC79 SDK (`uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin`, Apache-2.0, read at build time). `.wsl-setup.sh` sets all of
it up in WSL Ubuntu.

```
./build.sh                  # dev package
./build.sh --release 1.0    # release package, nighttrain-1.0.fwsc
./build.sh --host           # the simulator, app/build/host/nighttrain_host
sh tests/run_tests.sh       # all tests
```
