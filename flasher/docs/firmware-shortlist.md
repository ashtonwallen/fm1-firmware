# Firmware shortlist

Descriptions come from the fm1-custom-fw directory as read on 2026-11-23. Chip support is **not** recorded here on purpose: check each project's current release notes right before flashing (see CLAUDE.md rule 2). The Firmware Hub has a "Saves on newer FM-1s" filter: https://thefirmwarehub.com/#newchip

## Order to try

1. **Stock, as shipped.** Play it for a bit first so there's a baseline for sound, saving and USB audio.
2. **Official V16** (m-vave.com/download, 20 Oct 2026), if the unit shipped on something older.
3. First custom firmware: pick from below.

## Candidates

| Firmware | What it is | Notes |
|---|---|---|
| sloopDX / SLOOP | Live groovebox | USB audio streaming reported; had the new-chip fix early |
| Felucca | Multi-engine synth, 9 engines, 4 tracks | Most-forked base; several variants |
| Melodee | Prophet-5, CZ-1, DX7 engines, microtonal | Conflicting reports on new chip around 1.6.1 |
| X0X | Two 303s, 909, 808, breakbeat | |
| Hortator | 8-track drum machine | USB audio options |
| PIGEON | Sampler / drum machine | Samples over USB |
| Golana | microKORG-style analog synth | |
| Jangada | Dark groovebox, 10 engines | |
| GHOULBOX | Dungeon synth groovebox | |
| MAKU | Generative ambient | |
| ChoralRoot | Chord instrument | |
| TRIBE | 10-track groovebox | |
| Polly | 8-track arranger | |
| OberVave, Dinghy, Tonitor, Arctic Scream | Not yet looked at | Had the new-chip fix by mid-November |

## Tried

| Date | Firmware | Version | Result |
|---|---|---|---|
| 2026-11-23 | Penumbra | 1.9 | Flashed and verified; plays; settings survive power-off. Backup: `backups/penumbra-1.9/`. |
| 2026-11-23 | SLOOP | 2.8.5 | Flashed and verified, boots. Not wanted right now (loops). Backup: `backups/sloop-2.8.5/`. |
| 2026-11-23 | AMB-1 | 1.3.2 | Liked a lot. Backup with saved pieces: `backups/amb-1-1.3.2/`. |
| 2026-11-23 | NACHTFAHRT | 1.0.2 | Flashed and verified, boots. Sound over USB and save test pending. |
