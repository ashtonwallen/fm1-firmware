# FM-1 Flasher

Windows app for switching firmware on an M-VAVE FM-1 without bricking it. Wraps [M-Flasher](https://github.com/jvitkauskas/m-flasher).

## What it does

Pick a firmware from the list. The app then:

1. Reads the flash chip ID. It stops if it isn't the one expected (`EXPECTED_CHIP` in `scripts/fm1_flasher.py`).
2. Checks the file's SHA-256 against `catalog.json`.
3. Backs up the whole flash to `backups/auto/<firmware>/`. That backup becomes the firmware's saved version, so Restore can put back your settings later.
4. Writes the firmware, verifies it, and restarts the unit.
5. Runs `scripts/fm1-audio.ps1`, so the FM-1 plays into the PC's default output but never receives PC sound.

Everything runs under one admin prompt. Each step is appended to `docs/log.md`.

## Setup

- Windows, Python via [uv](https://github.com/astral-sh/uv).
- `tools/m-flasher/M-Flasher.exe` from M-Flasher's releases.
- Firmware files go where `catalog.json` says (`firmware/<id>/<version>/`). Get each one from its own project's releases and check its SHA-256. None are included here.
- Optional: `scripts/fm1-audio.local.json` names your preferred output and mic, e.g. `{ "outputs": ["Headphones"], "mics": ["USB Mic"] }`.

```
uv run --no-project python scripts/fm1_flasher.py              # the app
uv run --no-project python scripts/fm1_flasher.py --selftest   # checks only, writes nothing
powershell -File scripts/fm1-audio.ps1 status                  # audio setup, read-only
```

## Rules it follows

- Back up before every write.
- Check chip support before adding a firmware to the catalog. Newer units have a UC25HQ80 (`0xB36014`), and firmware made only for the old Puya chip (`0x856014`) may not save.
- Never touch the bootloader.
- One write at a time, then verify.
- If anything looks wrong, stop.

A firmware's USB name should contain "FM-1": M-Flasher only finds the unit by name.

`docs/firmware-shortlist.md` has notes on other FM-1 firmwares.
