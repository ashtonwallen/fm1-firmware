# fm1-firmware

Self-playing firmware for the M-VAVE FM-1 pocket synth, plus a Windows flasher. Each firmware is a small world on the screen that makes ambient music by itself. Characters, moods and events carry over between power-offs.

All of them are built on [AMB-1](https://github.com/charlesvestal/fm1-amb) by Charles Vestal, a generative ambient engine, running on [fm1-base](https://github.com/charlesvestal/fm1-base) and [felucca-bsp](https://github.com/hugelton/felucca-bsp).

## Firmwares

| Folder | What it is |
|---|---|
| [amb1-drift](amb1-drift) | AMB-1 with Wander: glides between 24 pieces by itself. Adds harp and kalimba, a strings pad, a bowed or glass lead, rain, wind, sea and vinyl textures, and reverb shimmer. |
| [tide-clock](tide-clock) | Music follows the time of day on a small coast. Tides, fog, storms, a lighthouse keeper, a gull, three boats. |
| [pond](pond) | Fish with names and temperaments, each singing a note. Fish that meet play a chord. Keys drop food. |
| [garden](garden) | Keys plant melody seeds that grow, flower, seed and wither. A gardener, bees, a rabbit, weather. |
| [night-train](night-train) | Endless night journey. Landscapes, stations, wheel clacks, passengers with stories. |
| [monastery](monastery) | Monks chant the hours over a drone. A novice who comes in early, a brother who sings flat. |
| [tavern](tavern) | Folk session. Regulars drift in, play generated tunes, fall out, leave at closing time. |
| [shortwave](shortwave) | Four ambient stations on a radio dial. Tune between them through static. |

Ready-built packages are on the [releases page](https://github.com/ashtonwallen/fm1-firmware/releases).

## Status

Early. Every firmware passes its own tests in the host simulator. Most haven't been played much on real hardware yet. They are built on fm1-base, which supports both the older Puya flash chip and the newer UC25HQ80.

## Flasher

[flasher](flasher) is a small Windows app around [M-Flasher](https://github.com/jvitkauskas/m-flasher). It does each install the same way:

- checks the flash chip
- checks the file's SHA-256
- backs up the whole flash
- writes and verifies
- restarts the unit
- sets up USB audio so the FM-1 plays into your PC without becoming its output

## Build

Each firmware builds in WSL Ubuntu with JieLi's toolchain. Run `.wsl-setup.sh` once, then in the firmware's folder:

```
./build.sh --release 1.0     # package in app/build/
./build.sh --host            # simulator
sh tests/run_tests.sh        # tests
```

Clone with `--recursive`: `base/` in each firmware is a git submodule.

## Licence

MPL-2.0, like AMB-1 and fm1-base. Fonts are under SIL OFL 1.1. See each folder's `LICENSING.md`. M-VAVE and FM-1 are trademarks of their owners. This project isn't affiliated with them.
