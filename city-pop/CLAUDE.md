# CITY POP

Endless generated 1980s Japanese city pop for the M-VAVE FM-1, on fm1-base, built from LOFI RADIO's tree (itself
from AMB-1 Drift, Charles Vestal's AMB-1, MPL-2.0). New engine and UI; AMB-1's platform glue, knob strip, help cards
and screen reader kept. Flashing goes through `../flasher` (its own safety rules); releases are copied to
`flasher/firmware/city-pop/X.Y/`.

## Build (WSL Ubuntu)

From Git Bash: `MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu -- bash -lc 'cd /mnt/d/Projects/FM1/city-pop && PYTHON=~/.venvs/fm1/bin/python <cmd>' | tr -d '\0'`

- `./build.sh` dev package `app/build/citypop.fwsc` (identity FM-1_6800)
- `./build.sh --release X.Y` release `app/build/citypop-X.Y.fwsc` (FM-1_68XXYYZZ); needs `docs/img/screen-city.png`
- `./build.sh --host` the simulator; `sh tests/run_tests.sh` all tests (run `./build.sh` first for the update path)
- Engine cost: `tests/host/citypop_test.c` prints ns/frame per style (2000 blocks warm, 10000 timed). Compare with
  AMB-1 Drift's busiest piece timed the same way on the same host (Low Tide, ~173 ns/frame at 1.0); CITY POP's
  busiest (Pocket Bell) ~160 ns/frame
- Reading the music: `citypop_test dump STYLE SONGS [all]` prints a song's plan (sections, chords bar by bar) and
  every note scheduled (`en_log`); `citypop_test wav STYLE SECONDS FROM OUT.wav` renders a stretch of one

Keep LF line endings. No doubles or libm in the engine (fastmath.h). No 64-bit division.

## Where things are

- Engine: `app/dsp/citypop.c` / `citypop.h`.
  - Parameters `PARAMS` / `P_*`; styles `STYLES` (recipe, keys Rhodes/DX, lead synth/sax/either, bass
    fingered/slap, feel, guitar part, rim-click verses, lush voicings, which verse and chorus progressions, sky).
  - Harmony: chord qualities `Q_*` (voicings `Q_VOICE`, melody sets `Q_CORE`/`Q_TENS`), progressions `VERSES`,
    `PRES` (end on a lift into the chorus), `CHORUSES`, `BRIDGES`; `voice4` voice-leads and avoids semitone rubs.
  - Grooves `GROOVES` (kick/snare/hat/bass strings, guitar cut and keys comp), `CUTS`, `ARPS`, `COMPS`, `RIFFS`.
  - Song plan: form, pushes (anticipations), key change (dominant 7sus4 then 7 into the new key), the melody
    written per song by `phrase()` (skeleton of chord tones, passing and neighbour notes between, phrase endings),
    repeated and transposed for the second verse, the choruses and the last chorus.
  - Voices: FM Rhodes/DX, Karplus-Strong guitar, FM bass (thumb, pop, ghost), saw lead, FM sax, FM brass, saw
    strings, drums with gated snare. Space: stereo chorus, FDN plate, dotted-eighth echo; Sparkle shelf, soft
    knee, leveller.
  - Titles: `T_ADJ`, `T_NOUN`, `T_TAIL` (checked against every other firmware's names; keep them unique).
  - Commands go main to render through a queue (`en_set`, `en_style`, `en_skip`...); UI reads `en_*` state.
- New parameters go at the end of the `P_*` enum, before `P_NPARAMS`; that changes the saved project, so bump
  `PROJ_FORMAT` in `app/app.h` and carry the old format over in `app/project.c`.
- UI: `app/ui.c`. Bands: city 0..107 (sky palettes `SKY`, hand-drawn skyline `NEAR` with rooftop signs, lights
  level `lights_target` per section), stereo 108..179 (cassette reels, display, meters), knobs 180..239. Pages
  Drive/Band/Front, help cards `help_lines`, autosave (knobs rested 10 s, settings at most every 5 min, song
  position at most hourly, only while paused or as the tape turns over), screen reader `speak`.
- Project: `app/project.c` (format 1, magic "CTP1"); anything else in the store gives the defaults.
- Host checks/commands: `app/host.c`; scenarios in `tests/scenarios/`; engine tests `tests/host/citypop_test.c`.
- Words for the screen reader: `app/words.tsv` (the url line points at the repo; nothing published yet).
- USB name "CITY POP FM-1" in `app/fm1.toml`: must keep "FM-1" so M-Flasher finds the unit.
- Demos (git-ignored): `../demos/city-pop/`, rendered with the host simulator (`wav` in a scenario).
