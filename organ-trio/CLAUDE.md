# ORGAN TRIO

Endless generated soul-jazz organ trio (tonewheel organ with rotating speaker, jazz guitar, drums; the organist's left
hand plays bass) for the M-VAVE FM-1, on fm1-base, built from JAZZ TRIO 1.0's tree (itself from Charles Vestal's
AMB-1, MPL-2.0). New engine and UI; AMB-1's platform glue, knob strip, help cards and screen reader kept. Flashing
goes through `../flasher` (its own safety rules); releases are copied to `flasher/firmware/organ-trio/X.Y/`.

## Build (WSL Ubuntu)

From Git Bash: `MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu -- bash -lc 'cd /mnt/d/Projects/FM1/organ-trio && PYTHON=~/.venvs/fm1/bin/python <cmd>' | tr -d '\0'`

- `./build.sh` dev package `app/build/organtrio.fwsc` (identity FM-1_5300)
- `./build.sh --release X.Y` release `app/build/organtrio-X.Y.fwsc` (FM-1_53XXYYZZ); needs `docs/img/screen-organ.png`
- `./build.sh --host` the simulator; `sh tests/run_tests.sh` all tests (run `./build.sh` first for the update path)
- Engine checks and cost: `tests/host/organ_test.c` (the CPU line: busiest band in ns/frame, against AMB-1 Low Tide's
  155-160 ns/frame on the same host; budget about 1.1x). Its other modes read the music:
  `organ_test dump BAND TUNES [STRETCH]` prints every bar's chords, bass, organ right hand, organ chords, guitar
  line and chords, drums (R ride, H hat, O open hat, S snare, K kick, T tom, C crash, B brush, X stick, r rim; '
  soft) and the organist's controls (swell, speaker, drawbars); `organ_test demo BAND SEC FILE [SKIP]` writes a WAV;
  `organ_test balance BAND` each player's level alone.
- In the dump, notes a lick carries over the barline are listed under the bar they sound in.

Keep LF line endings. No doubles or libm in the engine (fastmath.h). No 64-bit division.

## Where things are

- Engine: `app/dsp/organ.c` (parameters, bands `STYLES`, kinds `KINDS`, the command queue, voices: tonewheel organ
  from drawbar wavetables rebuilt as the drawbars move, percussion, key click, scanner chorus, preamp drive, the
  rotating speaker (horn Doppler line and two mics, drum AM, each rotor with its own acceleration), the bass voice
  through the drum rotor, Karplus-Strong guitar with a body, synthesized kit and brushes, the crowd, reverb, bus),
  which includes:
  - `organ_harm.c`: chord qualities, rootless voicings, forms (blues, shuffle blues, minor blues, grooves, gospel
    8+8, 32-bar AABA), drawbar registrations `REG_UP`/`REG_LO`/`REG_FULL`, titles.
  - `organ_tune.c`: a tune: its form, heads (riffs, minor licks, groove riffs, gospel lyric, bebop), the line
    player `line_bar`, licks fitted to the chord (`LICK4`, `LICK12`, `fit`, `lick_put`), the arrangement
    (`arrange`: intro, head, solos by Focus, fours, head out, tag/amen/rit/Basie/stop ending; `BF_PEAK` marks the
    organ's last chorus) and the set (`pick_kind`, `new_tune`).
  - `organ_play.c`: the players bar by bar (drums per feel, walking/two-feel/12-8/groove bass, organ comping and
    solos: licks AAB, lines, the shout with riffs, tails and a held chord; guitar comping per kind and solos
    single notes, octaves, chords), the organist's controls (speaker fast at the peak, on long head notes, behind
    the guitar's last bars; drawbars pulled out at the peak; the swell pedal), timing (`swing_map`, `seq_beat`),
    applause and the gap between tunes.
- New parameters go at the end of the `P_*` enum, before `P_NPARAMS`; that changes the saved project, so bump
  `PROJ_FORMAT` in `app/app.h` and carry the old format over in `app/project.c`.
- UI: `app/ui.c`. Pages Band/Mix/Organ/Set (HOME/ENV/LFO/GLO). The scene: `bg_build` draws the console, manuals at
  rest and the cabinet once into the pool; each frame the drawbars (`en_draw`), the keys down (`en_keys_up`,
  `en_keys_lo`, the bass note), the nameplate (title, part, chord) and the speaker (`en_horn_ph`, `en_drum_ph`,
  ghosts when fast). Help cards `help_lines`, autosave (knobs rested 10 s, settings at most every 5 min, the set's
  position at most hourly, only on a break or between tunes), screen reader `speak`.
- Project: `app/project.c` (format 1, magic "ORG1"); anything else in the store gives the defaults.
- Host checks/commands: `app/host.c` (`storeamb`, `storejazz`, `storejunk`); scenarios in `tests/scenarios/`.
- Words for the screen reader: `app/words.tsv`.
- USB name "ORGAN TRIO FM-1" in `app/fm1.toml`: must keep "FM-1" so M-Flasher finds the unit.
- Names (bands, title words) must not repeat anywhere in the FM-1 repo: check with
  `rg -i -w "<name>" .. --glob '!**/base/**' --glob '!**/build/**'`.
