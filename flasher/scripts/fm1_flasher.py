"""FM-1 Flasher: pick a firmware or a saved backup and put it on the FM-1.

Asks for admin once when it opens, so the M-Flasher steps inside don't each ask again.
Every write follows the project's rules (CLAUDE.md): checksum, backup of the current
state first, one write, verify, restart, audio setup, log. It stops at the first problem.

    uv run --no-project python scripts/fm1_flasher.py              # the app (asks for admin)
    uv run --no-project python scripts/fm1_flasher.py --selftest   # checks only, no admin, nothing written
"""

import ctypes
import datetime as dt
import hashlib
import json
import os
import queue
import re
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MFLASHER = ROOT / "tools" / "m-flasher" / "M-Flasher.exe"
CATALOG = ROOT / "catalog.json"
BACKUPS = ROOT / "backups"
LOG_MD = ROOT / "docs" / "log.md"
AUDIO_PS1 = ROOT / "scripts" / "fm1-audio.ps1"
EXPECTED_CHIP = "0xB36014"
NO_WINDOW = 0x08000000


class StepFailed(Exception):
    pass


# ---------- M-Flasher and device steps ----------

def run(cmd, timeout=300):
    p = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace",
                       timeout=timeout, creationflags=NO_WINDOW, cwd=ROOT, stdin=subprocess.DEVNULL)
    out = re.sub(r"(\s+\d{1,3}%)+", "", (p.stdout or "") + (p.stderr or ""))
    return p.returncode, out.strip()


def mf(*args, timeout=300):
    return run([str(MFLASHER), *[str(a) for a in args]], timeout=timeout)


def find_state():
    """('running', name) | ('download', drive) | (None, text)."""
    _, out = mf("find", timeout=30)
    for line in out.splitlines():
        if line.startswith("midi"):
            name = line.split("\t", 1)[1].split(" (running")[0] if "\t" in line else line
            return "running", name.strip()
        if "UBOOT" in line:
            return "download", line.split("\t", 1)[0]
    return None, out


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def slug(text):
    return re.sub(r"[^a-z0-9.]+", "-", text.lower()).strip("-") or "unknown"


class Device:
    def __init__(self, say):
        self.say = say

    def step(self, label, *args, need=None, timeout=300):
        self.say(f"> {label}")
        code, out = mf(*args, timeout=timeout)
        if out:
            self.say(out)
        needs = (need,) if isinstance(need, str) else need
        if code != 0 or (needs and not any(n in out for n in needs)):
            raise StepFailed(f"{label} did not finish as expected (exit code {code}).")
        return out

    def to_download_mode(self):
        state, _ = find_state()
        if state == "download":
            self.say("Already in download mode.")
            return
        if state != "running":
            raise StepFailed("No FM-1 found on USB. Check the cable (straight into the PC, no hub).")
        self.step("Download mode", "download-mode", timeout=60)
        for _ in range(20):
            if find_state()[0] == "download":
                return
            time.sleep(1)
        raise StepFailed("The FM-1 did not show up in download mode.")

    def info(self):
        out = self.step("Read device info", "info", need="CRC32 of the flash matches", timeout=120)
        fw = re.search(r"^\s*firmware\s+(.+)$", out, re.M)
        chip = re.search(r"^\s*flash\s+(0x[0-9A-Fa-f]+)", out, re.M)
        boot = re.search(r"^\s*bootloader\s+(.+)$", out, re.M)
        return {
            "firmware": fw.group(1).strip() if fw else "unknown",
            "chip": chip.group(1) if chip else "unknown",
            "bootloader": boot.group(1).strip() if boot else "unknown",
        }

    def backup(self, folder):
        folder.mkdir(parents=True, exist_ok=True)
        out = self.step("Back up the whole flash", "backup", folder, need="CRC32 of the flash matches", timeout=120)
        m = re.search(r"Backup saved:\s*(.+\.bin)", out)
        if not m:
            raise StepFailed("The backup did not report a file.")
        path = Path(m.group(1).strip())
        if not path.exists() or path.stat().st_size != 1 << 20:
            raise StepFailed(f"The backup file looks wrong: {path}")
        for f in (path, path.with_suffix(".json")):
            if f.exists():
                os.chmod(f, 0o444)
        return path, sha256(path)

    def write_firmware(self, fwsc):
        return self.step("Write the firmware", "firmware", fwsc, "--yes",
                         need=("everything written is verified", "flash already holds this"), timeout=300)

    def write_restore(self, bin_path):
        return self.step("Write the backup back", "restore", bin_path, "--keep-bootloader", "--yes",
                         need=("verified", "flash already holds this"), timeout=300)

    def restart(self):
        self.step("Restart", "restart", timeout=60)
        for _ in range(25):
            state, name = find_state()
            if state == "running":
                self.say(f"Running: {name}")
                return name
            time.sleep(1)
        raise StepFailed("The FM-1 did not come back after the restart. Don't write anything else; see CLAUDE.md rule 5.")

    def audio(self):
        self.say("> PC audio setup")
        code, out = run(["pwsh", "-NoProfile", "-File", str(AUDIO_PS1), "apply"], timeout=120)
        self.say(out)
        if code != 0:
            self.say("(Audio setup reported a problem; the firmware itself is fine.)")


# ---------- catalog, backups, log ----------

# Per-unit state (which backup is each firmware's saved version, favourites) lives in state.json, kept out of git;
# catalog.json holds only what is true for every FM-1.
STATE = ROOT / "state.json"
PRIVATE = ("backup", "favorite")


def load_catalog():
    cat = json.loads(CATALOG.read_text(encoding="utf-8"))
    state = json.loads(STATE.read_text(encoding="utf-8")) if STATE.exists() else {}
    for fw in cat["firmwares"]:
        mine = state.get(fw["id"], {})
        for k in PRIVATE:
            if k in mine or k not in fw:
                fw[k] = mine.get(k, None if k == "backup" else False)
    return cat


def save_catalog(cat):
    state = json.loads(STATE.read_text(encoding="utf-8")) if STATE.exists() else {}
    public = json.loads(json.dumps(cat))
    for fw in public["firmwares"]:
        mine = {k: fw.pop(k) for k in PRIVATE if k in fw}
        mine = {k: v for k, v in mine.items() if v}
        if mine:
            state[fw["id"]] = mine
        else:
            state.pop(fw["id"], None)
    STATE.write_text(json.dumps(state, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    CATALOG.write_text(json.dumps(public, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def name_for(info_firmware, cat):
    if "stock" in info_firmware.lower():
        return "stock"
    for fw in cat["firmwares"]:
        if fw.get("info_name") and info_firmware.startswith(fw["info_name"]):
            return fw["name"]
    return info_firmware


def list_backups():
    items = []
    for p in sorted(BACKUPS.rglob("*.bin")):
        rel = p.relative_to(ROOT).as_posix()
        label = p.parent.relative_to(BACKUPS).as_posix()
        stamp = p.stem.replace("fm1-", "")
        items.append((f"{label}  ({stamp})", rel))
    return items


def log(lines):
    today = dt.date.today().isoformat()
    text = LOG_MD.read_text(encoding="utf-8")
    add = ""
    if f"## {today}" not in text:
        add += f"\n## {today}\n\n"
    elif not text.endswith("\n"):
        add += "\n"
    add += "".join(f"{line}\n" for line in lines)
    with open(LOG_MD, "a", encoding="utf-8", newline="\n") as f:
        f.write(add)


# ---------- the jobs ----------

def job_install(fw, say):
    """Checksum, back up what's on it now, write, verify, restart, audio, log."""
    cat = load_catalog()
    path = ROOT / fw["file"]
    say(f"Checking {path.name}")
    if not path.exists():
        raise StepFailed(f"{fw['file']} is missing.")
    if sha256(path) != fw["sha256"]:
        raise StepFailed("The firmware file's SHA-256 doesn't match the catalog. Not writing it.")
    say("SHA-256 matches.")
    return _write(fw["name"] + " " + fw["version"], say, cat,
                  lambda d: d.write_firmware(path),
                  [f"`{fw['file']}`, SHA-256 `{fw['sha256'][:8]}...` matched the catalog."],
                  installing_id=fw["id"])


def job_restore(label, bin_rel, say):
    cat = load_catalog()
    path = ROOT / bin_rel
    if not path.exists():
        raise StepFailed(f"{bin_rel} is missing.")
    return _write(f"backup {label}", say, cat, lambda d: d.write_restore(path),
                  [f"Restored `{bin_rel}` (SHA-256 `{sha256(path)[:8]}...`) with --keep-bootloader."])


def _write(what, say, cat, do_write, notes, installing_id=None):
    d = Device(say)
    d.to_download_mode()
    before = d.info()
    if before["chip"] != EXPECTED_CHIP:
        raise StepFailed(f"Flash chip is {before['chip']}, not {EXPECTED_CHIP}. Is this the same FM-1? Stopping.")
    current = name_for(before["firmware"], cat)
    say(f"On it now: {current}")
    bpath, bsha = d.backup(BACKUPS / "auto" / slug(current))
    say(f"Backup: {bpath.relative_to(ROOT).as_posix()}  SHA-256 {bsha}")
    for fw in cat["firmwares"]:  # the backup becomes that firmware's saved version
        if fw["name"] == current:
            fw["backup"] = bpath.relative_to(ROOT).as_posix()
            save_catalog(cat)
    do_write(d)
    after = d.info()
    if after["chip"] != EXPECTED_CHIP or "stock" not in after["bootloader"].lower():
        say(f"Note: bootloader now reads '{after['bootloader']}'.")
    if installing_id:  # learn how M-Flasher names it, so the next backup gets the right label
        cat = load_catalog()
        for fw in cat["firmwares"]:
            if fw["id"] == installing_id and not fw.get("info_name"):
                fw["info_name"] = after["firmware"]
                save_catalog(cat)
    running_as = d.restart()
    d.audio()
    log([f"- Flasher app: put **{what}** on the FM-1 (was {current}).",
         f"  - Backup first: `{bpath.relative_to(ROOT).as_posix()}`, SHA-256 `{bsha}`."]
        + [f"  - {n}" for n in notes]
        + [f"  - Verified (chip CRC32). `info`: firmware {after['firmware']}, bootloader {after['bootloader']}. Running as \"{running_as}\"."])
    return f"Done: {what} is on the FM-1 and running."


# ---------- the window ----------

def run_app(smoke=False):
    import tkinter as tk
    from tkinter import messagebox, ttk

    BG, PANEL, FG, DIM, ACC = "#16161a", "#202026", "#e8e6e3", "#9a978f", "#e0a458"
    root = tk.Tk()
    root.title("FM-1 Flasher")
    root.geometry("980x680")
    root.configure(bg=BG)
    style = ttk.Style(root)
    style.theme_use("clam")
    style.configure(".", background=BG, foreground=FG, fieldbackground=PANEL, font=("Segoe UI", 10))
    style.configure("TButton", background=PANEL, foreground=FG, padding=(12, 6), borderwidth=0)
    style.map("TButton", background=[("active", "#2c2c34"), ("disabled", BG)], foreground=[("disabled", "#55534f")])
    style.configure("Accent.TButton", background=ACC, foreground="#16161a")
    style.map("Accent.TButton", background=[("active", "#f0b46a"), ("disabled", "#3a3328")])
    style.configure("TCombobox", fieldbackground=PANEL, background=PANEL, foreground=FG, arrowcolor=FG)

    q = queue.Queue()
    busy = {"on": False}
    cat = {"data": load_catalog()}

    top = tk.Frame(root, bg=BG)
    top.pack(fill="x", padx=16, pady=(14, 6))
    tk.Label(top, text="FM-1 Flasher", bg=BG, fg=FG, font=("Segoe UI Semibold", 16)).pack(side="left")
    status = tk.Label(top, text="Looking for the FM-1...", bg=BG, fg=DIM)
    status.pack(side="left", padx=16)

    body = tk.Frame(root, bg=BG)
    body.pack(fill="both", expand=True, padx=16)
    lst = tk.Listbox(body, bg=PANEL, fg=FG, selectbackground=ACC, selectforeground="#16161a", activestyle="none",
                     borderwidth=0, highlightthickness=0, font=("Segoe UI", 11), width=30)
    lst.pack(side="left", fill="y", pady=4)
    right = tk.Frame(body, bg=BG)
    right.pack(side="left", fill="both", expand=True, padx=(14, 0))
    title = tk.Label(right, text="", bg=BG, fg=FG, font=("Segoe UI Semibold", 14), anchor="w")
    title.pack(fill="x")
    sub = tk.Label(right, text="", bg=BG, fg=ACC, anchor="w")
    sub.pack(fill="x")
    detail = tk.Text(right, bg=BG, fg=FG, wrap="word", borderwidth=0, highlightthickness=0, height=12,
                     font=("Segoe UI", 10))
    detail.pack(fill="both", expand=True, pady=(6, 6))
    btns = tk.Frame(right, bg=BG)
    btns.pack(fill="x")
    b_install = ttk.Button(btns, text="Install this firmware", style="Accent.TButton")
    b_install.pack(side="left")
    b_mine = ttk.Button(btns, text="Restore my saved version")
    b_mine.pack(side="left", padx=8)
    b_fav = ttk.Button(btns, text="☆ Favourite")
    b_fav.pack(side="left")
    rest = tk.Frame(right, bg=BG)
    rest.pack(fill="x", pady=(10, 0))
    tk.Label(rest, text="Any backup:", bg=BG, fg=DIM).pack(side="left")
    backups = list_backups()
    combo = ttk.Combobox(rest, values=[b[0] for b in backups], state="readonly", width=48)
    combo.pack(side="left", padx=8)
    b_restore = ttk.Button(rest, text="Restore")
    b_restore.pack(side="left")
    out = tk.Text(root, bg="#0f0f12", fg="#bdbab3", height=11, borderwidth=0, highlightthickness=0,
                  font=("Cascadia Mono", 9), wrap="word")
    out.pack(fill="x", padx=16, pady=(8, 14))

    def say(text):
        q.put(("log", text))

    def items():
        fws = cat["data"]["firmwares"]
        return sorted(fws, key=lambda f: (not f.get("favorite"), f["name"].lower()))

    def fill_list():
        sel = lst.curselection()
        lst.delete(0, "end")
        for fw in items():
            lst.insert("end", f"{'★ ' if fw.get('favorite') else '   '}{fw['name']}  {fw['version']}")
        lst.selection_set(sel[0] if sel else 0)
        show()

    def selected():
        s = lst.curselection()
        return items()[s[0]] if s else None

    def show(_=None):
        fw = selected()
        if not fw:
            return
        title.config(text=f"{fw['name']}  {fw['version']}")
        sub.config(text=fw.get("kind", ""))
        detail.config(state="normal")
        detail.delete("1.0", "end")
        detail.insert("end", fw["about"] + "\n\nHow to use it\n" + fw["how"]
                      + "\n\nYour chip: " + fw["chip"] + "\nSound over USB: " + fw.get("usb", "Not stated."))
        detail.config(state="disabled")
        b_fav.config(text="★ Favourite" if fw.get("favorite") else "☆ Favourite")
        set_buttons()

    def set_buttons():
        fw = selected()
        on = not busy["on"]
        b_install.state(["!disabled"] if on else ["disabled"])
        b_mine.state(["!disabled"] if on and fw and fw.get("backup") else ["disabled"])
        b_restore.state(["!disabled"] if on and combo.get() else ["disabled"])

    def confirm(what):
        return messagebox.askokcancel(
            "Write to the FM-1?",
            f"Put {what} on the FM-1?\n\n"
            "First the whole flash is backed up, then this is written and verified, "
            "and the FM-1 restarts.\n\n"
            "• The FM-1 is charged and plugged straight into the PC (no hub)\n"
            "• No DAW or browser tab is using its MIDI\n"
            "• Don't unplug it until this window says Done")

    def start(job, *args):
        busy["on"] = True
        set_buttons()
        out.delete("1.0", "end")

        def worker():
            try:
                q.put(("done", True, job(*args, say)))
            except StepFailed as e:
                q.put(("done", False, f"Stopped: {e}\nNothing more was written. Check the steps above before trying anything else."))
            except Exception as e:  # noqa: BLE001
                q.put(("done", False, f"Stopped on an unexpected error: {e!r}"))
        threading.Thread(target=worker, daemon=True).start()

    def on_install():
        fw = selected()
        if fw and confirm(f"{fw['name']} {fw['version']}"):
            start(job_install, fw)

    def on_mine():
        fw = selected()
        if fw and fw.get("backup") and confirm(f"your saved {fw['name']} ({fw['backup']})"):
            start(job_restore, fw["name"], fw["backup"])

    def on_restore():
        label = combo.get()
        rel = dict(backups).get(label)
        if rel and confirm(f"the backup {label}"):
            start(job_restore, label, rel)

    def on_fav():
        fw = selected()
        if not fw:
            return
        data = load_catalog()
        for f in data["firmwares"]:
            if f["id"] == fw["id"]:
                f["favorite"] = not f.get("favorite")
        save_catalog(data)
        cat["data"] = data
        fill_list()

    def check_device():
        def w():
            state, name = find_state()
            q.put(("status", f"FM-1 running: {name}" if state == "running"
                   else "FM-1 in download mode" if state == "download" else "No FM-1 found on USB"))
        threading.Thread(target=w, daemon=True).start()

    def pump():
        try:
            while True:
                msg = q.get_nowait()
                if msg[0] == "log":
                    out.insert("end", msg[1] + "\n")
                    out.see("end")
                elif msg[0] == "status":
                    status.config(text=msg[1])
                elif msg[0] == "done":
                    busy["on"] = False
                    out.insert("end", "\n" + msg[2] + "\n")
                    out.see("end")
                    cat["data"] = load_catalog()
                    nonlocal_backups()
                    fill_list()
                    check_device()
                    (messagebox.showinfo if msg[1] else messagebox.showerror)("FM-1 Flasher", msg[2])
        except queue.Empty:
            pass
        root.after(100, pump)

    def nonlocal_backups():
        backups[:] = list_backups()
        combo.config(values=[b[0] for b in backups])

    lst.bind("<<ListboxSelect>>", show)
    combo.bind("<<ComboboxSelected>>", lambda _: set_buttons())
    b_install.config(command=on_install)
    b_mine.config(command=on_mine)
    b_restore.config(command=on_restore)
    b_fav.config(command=on_fav)
    fill_list()
    check_device()
    pump()
    if smoke:
        root.after(2500, root.destroy)
    root.mainloop()


# ---------- entry ----------

def selftest():
    ok = True
    cat = load_catalog()
    print(f"M-Flasher: {'found' if MFLASHER.exists() else 'MISSING'}")
    for fw in cat["firmwares"]:
        p = ROOT / fw["file"]
        good = p.exists() and sha256(p) == fw["sha256"]
        b = fw.get("backup")
        bgood = (ROOT / b).exists() if b else None
        ok &= good and bgood is not False
        print(f"{fw['name']:<11} {fw['version']:<6} file+sha {'OK' if good else 'BAD'}  backup {b or '-'} {'' if bgood is None else ('OK' if bgood else 'MISSING')}")
    print("Name mapping:", {s: name_for(s, cat) for s in ["FM-1 015 (stock)", "FM-1 701", "FM-1 197", "SLOOP 2.8.5", "FM-1 999"]})
    print("Backups:", [b[0] for b in list_backups()])
    print("Device:", find_state())
    return ok


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        sys.exit(0 if selftest() else 1)
    if "--smoke" in sys.argv:
        run_app(smoke=True)
        print("window opened and closed OK")
        sys.exit(0)
    if not ctypes.windll.shell32.IsUserAnAdmin():
        exe = sys.executable
        w = Path(exe).with_name("pythonw.exe")
        ctypes.windll.shell32.ShellExecuteW(None, "runas", str(w if w.exists() else exe), f'"{__file__}"', str(ROOT), 1)
        sys.exit(0)
    run_app()
