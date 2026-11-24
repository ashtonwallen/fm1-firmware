# Shortwave

Shortwave turns the M-VAVE FM-1 into a fictional ambient radio. Four stations play continuously. Tuning between them blends their music and adds soft hiss and pitch drift. The 5–15 MHz display is simulated. It has a spectrum, a scrolling waterfall, and a signal meter.

| Control | Action |
| --- | --- |
| SELECT | Fine tune and hold the dial |
| PRESETS | Tune to the previous or next station |
| HOME | Center on the nearest station |
| SEQ | Toggle the automatic scan |
| PLAY | Pause or resume |
| KNOB 1 | Set the amount of tuning noise |
| KNOB 2 | Set the amount of pitch drift |
| MASTER | Set both analog and USB audio level |
| SAVE | Store tuning, scan mode, noise, and motion |

Every boot starts playback. The default auto scan holds a station for 40 seconds, then sweeps to the next over 18 seconds. Saving in hold mode resumes that tuning when you turn the FM-1 back on. The USB device is **SHORTWAVE FM-1** and sends audio to the PC.

## Build and verify

The platform is fm1-base at `3ae2a65`, with felucca-bsp at `f878800`. It supports the UC25HQ80 chip (`0xB36014`). Shortwave uses package identity prefix `FM-1_82` and its own settings magic.

From WSL Ubuntu in this directory, using the existing JieLi toolchain, AC79 SDK, and Python environment:

```sh
PYTHON=~/.venvs/fm1/bin/python sh build.sh --release 1.0
PYTHON=~/.venvs/fm1/bin/python sh tests/run_tests.sh app/build/shortwave-1.0.fwsc
```

The release for the desktop app is `app/build/shortwave-1.0.fwsc`. Use FM-1 Flasher for installation and follow the safety rules in `../flasher/CLAUDE.md`. The tests run entirely offline. These include a simulated update that checks the stock bootloader region stays intact.

Shortwave is MPL-2.0. It uses Charles Vestal’s fm1-base and Leo Kuroshita’s felucca-bsp with their original notices.
