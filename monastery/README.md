# MONASTERY for the M-VAVE FM-1

Monks sing the hours in a stone chapel. Self-playing firmware for the M-VAVE FM-1, built on AMB-1 by Charles Vestal.

![The chapel at Lauds](docs/img/screen-chapel.png) ![The brothers](docs/img/screen-brothers.png)

Seven brothers chant modal psalm tones over a drone, through a long reverb. A bell marks each office, Matins to
Compline. Each has its own mode, pace and choir: everyone at Lauds and Vespers, three at Sext. A day takes two hours.

How it's sung:

- **Benedict**, the abbot, intones each office.
- **Gregory** and **Bernard**, the cantors, lead the antiphons. Some psalms they trade verses.
- Verses go side to side, or a soloist sings and the choir answers.
- **Ambrose** sings organum, a fifth under, at Lauds and Vespers. **Columba** holds the ison at Matins, Sext, Compline.
- **Anselm**, the novice, starts in the choruses. Gets the verses, then solos, then takes his vows.
- **Bede** turns up at the gate after some days.

Nobody sings a wrong note. The brothers, the day and the chronicle survive power-offs.

## Presets

Twelve places: Abbey Chapel, Cathedral, Hermitage, Cloister, Mountain Abbey, Byzantine, Candlelight, Island Abbey,
Crypt, Snow Priory, Pilgrims, Easter Vigil. Turn **PRESETS**.

## Controls

| | |
|---|---|
| PLAY | Sing / rest |
| EDIT | Bell: next office now |
| REC | Hold the note |
| OCT− / OCT+ | Slower / faster chant |
| White keys 1–7 | A brother sings the next verse alone |
| HOME · ARP · FX · GLO | Chapel (Voices, Drone, Bell, Reverb) · Brothers · Space (Echo, Size, Colour, Ambience) · Setup (Day length, Tune, MIDI out, Lights) |
| Hold a button | Help card |
| SAVE | Save. Held: sound back to defaults |

**Ambience** sets the rain, wind, sea or candle crackle, off to 120 %, per preset.

Saves itself while nobody sings: settings within 5 minutes, the world hourly. 1.0 and 1.1 saves load.

## Build

See `CLAUDE.md` and `BUILDING.md`.

## Credits

Engine, UI and platform glue from [AMB-1](https://github.com/charlesvestal/fm1-amb) by Charles Vestal; strings,
lead, air and shimmer from AMB-1 Drift 1.1. Platform: [fm1-base](https://github.com/charlesvestal/fm1-base) and
[felucca-bsp](https://github.com/hugelton/felucca-bsp), from Felucca by Leo Kuroshita (Hügelton Instruments). All
MPL-2.0; see `LICENSING.md`. Choir and chapel: ashtonwallen.
