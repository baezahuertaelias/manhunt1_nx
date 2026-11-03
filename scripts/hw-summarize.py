#!/usr/bin/env python3
"""Summarise one physical-Switch run from the files scripts/hw-run.sh collected.

Usage: python3 -I scripts/hw-summarize.py <run-dir>

Reads (all optional, whatever exists): tcp.log (live TCP stream), log.txt (SD log),
wd.txt, ping.log, frames/, crash_reports.new, fatal_errors.new. Writes
<run-dir>/summary.md and prints it. It reports FACTS only; it does not decide
pass or fail, and it never claims what it did not see. Standard library only.
"""
import os
import re
import sys


def read(path):
    try:
        with open(path, "rb") as f:
            return f.read().decode("utf-8", "replace")
    except OSError:
        return ""


def first(rx, text, flags=0):
    m = re.search(rx, text, flags)
    return m.group(0).strip() if m else None


def bmp_rows(path):
    """Return (width, height, getpixel(x, y)->(r,g,b)) for an uncompressed 24/32-bit BMP, or None."""
    try:
        with open(path, "rb") as f:
            d = f.read()
        if d[:2] != b"BM":
            return None
        off = int.from_bytes(d[10:14], "little")
        w = int.from_bytes(d[18:22], "little", signed=True)
        h = int.from_bytes(d[22:26], "little", signed=True)
        bpp = int.from_bytes(d[28:30], "little")
        if bpp not in (24, 32) or int.from_bytes(d[30:34], "little") != 0:
            return None
        bytes_pp = bpp // 8
        stride = (w * bytes_pp + 3) & ~3
        top_down = h < 0
        h = abs(h)
        if w <= 0 or h <= 0 or len(d) < off + stride * h:
            return None                 # truncated or damaged file (e.g. the game was closed mid-write)

        def px(x, y):
            row = y if top_down else h - 1 - y
            i = off + row * stride + x * bytes_pp
            return d[i + 2], d[i + 1], d[i]
        return w, h, px
    except (OSError, IndexError):
        return None


# Menu rows of the original main menu at 640x480 (PLAY, SELECT SCENE, LOAD GAME, SETTINGS, BONUS FEATURES):
# the highlighted entry is a light filled box behind its text, so it is the row with the brightest left strip.
MENU_ROW_Y = (205, 233, 262, 290, 319)
MENU_NAMES = ("PLAY", "SELECT SCENE", "LOAD GAME", "SETTINGS", "BONUS FEATURES")


def highlighted_row(path):
    """Index of the highlighted main-menu entry, or None when the frame is not recognisably the menu.

    The highlight is a large flat grey box behind the entry (calibrated on real console frames: about
    0.68 of the sampled area at full size, 0.30 when the glitch band hides part of it; text rows stay
    under 0.15; the legal screen and half-written black frames score nothing like it)."""
    r = bmp_rows(path)
    if not r or r[0] != 640 or r[1] != 480:
        return None
    _, _, px = r
    scores = []
    for y in MENU_ROW_Y:
        cnt = tot = 0
        for yy in range(y - 12, y + 13):
            for xx in range(362, 409):
                c = px(xx, yy)
                g = sum(c) / 3.0
                tot += 1
                if 100 <= g <= 145 and abs(c[0] - c[2]) <= 14 and abs(c[0] - c[1]) <= 14:
                    cnt += 1
        scores.append(cnt / float(tot))
    order = sorted(range(len(scores)), key=lambda i: -scores[i])
    if scores[order[0]] < 0.25 or scores[order[1]] > 0.2:
        return None
    return order[0]


def main(run):
    tcp = read(os.path.join(run, "tcp.log"))
    sdlog = read(os.path.join(run, "log.txt"))
    wd = read(os.path.join(run, "wd.txt"))
    ping = read(os.path.join(run, "ping.log"))
    # The stream is the live view; the SD log is the fallback and the cross-check.
    log = tcp if len(tcp) >= len(sdlog) else sdlog
    src = "tcp.log" if log is tcp else "log.txt"
    out = []
    p = out.append

    p("# Hardware run summary")
    p("")
    p("Run directory: `%s`. Facts below are parsed from the run's own files (`%s`, %d bytes); nothing is judged pass/fail." % (os.path.basename(run.rstrip("/")), src, len(log)))
    if not log:
        p("")
        p("**No log was captured.** Without a stream or SD log there is nothing to summarise (UNVERIFIED).")

    # ---- configuration sanity: the diagnostics that change behaviour
    p("")
    p("## Configuration seen by the game")
    cfg = re.findall(r"\[MH-BOOT\] config (\S+)", log + "\n" + sdlog)
    cfg = sorted(set(cfg), key=cfg.index)
    p("- Keys read from manhunt.env: %s" % (", ".join(cfg) if cfg else "none logged (the TCP stream connects after the config lines; pull log.txt from the SD card)"))
    if "[WATCHDOG] no exit after" in log or any(k.startswith("RECOMP_WATCHDOG_SECS") for k in cfg):
        p("- **WARNING: the runtime watchdog (`RECOMP_WATCHDOG_SECS`) was active: it ends the process with `_exit(3)`. The run length says nothing about stability.**")
    for k in ("applet type", "applet focus state", "focus handling"):
        v = first(r"\[MH-BOOT\] " + k + r".*", log)
        if v:
            p("- %s" % v)

    # ---- memory layer
    p("")
    p("## Memory layer")
    hz = re.findall(r"\[HZ\] (create|map)( FAILED)? (\d+) KB -> 0x([0-9a-f]+) \(live code-memory objects (\d+)\)", log)
    if hz:
        live = max(int(m[4]) for m in hz)
        fails = [m for m in hz if m[1]]
        p("- Code-memory create/map calls logged: %d; highest live object count: %d (kernel limit for the whole console: 10); failures: %d" % (len(hz), live, len(fails)))
    else:
        p("- No `[HZ]` lines (build without patch 0016, or nothing logged).")
    ap = re.findall(r"WARNING: (NV2A|MCPX|flash) aperture at 0x[0-9A-Fa-f]+ failed \(error (\d+)\)", log)
    p("- Aperture mapping failures: %s" % (", ".join("%s (error %s)" % a for a in ap) if ap else "none"))
    mo = first(r"memory offset=0x[0-9a-f]+", log)
    if mo:
        p("- Guest window base: `%s`" % mo)

    # ---- progress through the title
    p("")
    p("## Progress")
    files = re.findall(r"\[FILE\] (\S+) \(disp", log)
    p("- Data files opened by the game: %d" % len(files))
    if files:
        p("- Last files opened: %s" % ", ".join("`%s`" % f for f in files[-3:]))
    marks = {
        "MAIN_EX.TXD": "main-menu texture archive opened",
        "FE_M.RIB": "front-end music stream opened",
        "levels.txt": "level list read",
        "GAME.SCC": "game script read",
    }
    for key, what in marks.items():
        if any(key.lower() in f.lower() for f in files):
            p("- %s (`%s`)" % (what, key))
    pres = re.findall(r"\(presents=(\d+)\)", log)
    first_present = "[MH-FRAME] present 1" in log
    p("- Frames presented: %s%s" % (pres[-1] if pres else ("at least 1" if first_present else "0 seen"), " (from the periodic frame dump)" if pres else ""))
    vb = re.findall(r"\[NV2A\] vblank (\d+): last 600 in (\d+) ms", log)
    if vb:
        rate = 600000.0 / float(vb[-1][1])
        p("- Vblank timer: %s ms per 600 (%.1f Hz)" % (vb[-1][1], rate))
    pix = re.findall(r"\[GPU\] (\d+) pixels written", log)
    tri = re.findall(r"\[GPU\] rasterised (\d+) triangles", log)
    if pix and tri:
        p("- Software rasteriser totals at the last report: %s pixels, %s triangles" % (pix[-1], tri[-1]))
    spin = re.findall(r"ordinal (\d+) x(\d+)", log)
    if spin:
        top = max(spin, key=lambda t: int(t[1]))
        p("- Most frequent kernel call in the last summary: ordinal %s x%s (a very high count means the guest is spinning in a wait loop)" % top)

    # ---- render profile (only with RECOMP_RENDER_PROFILE=1)
    rp = re.findall(r"\[RENDER-PROFILE\].*", log)
    if rp:
        p("")
        p("## Render profile (last report; scopes overlap, do not add them)")
        for line in rp[-8:]:
            p("- `%s`" % line.strip())
        last = {}
        for line in rp[-12:]:
            m = re.match(r"\[RENDER-PROFILE\] (\w+) calls=(\d+) ns=(\d+)", line.strip())
            if m:
                last[m.group(1)] = (int(m.group(2)), int(m.group(3)))
            m = re.match(r"\[RENDER-PROFILE\] flip_intervals=(\d+) ns=(\d+)", line.strip())
            if m:
                last["flip"] = (int(m.group(1)), int(m.group(2)))
        if "raster" in last and "flip" in last and last["flip"][1] > 0:
            rc, rn = last["raster"]
            fc, fn = last["flip"]
            p("- Derived: raster wall time is %.0f%% of the flip intervals (%.1f s of %.1f s); about %.1f ms per raster call; flip interval about %.2f s per frame." % (100.0 * rn / fn, rn / 1e9, fn / 1e9, rn / 1e6 / max(rc, 1), fn / 1e9 / max(fc, 1)))
            if pix:
                p("- Derived: about %.2f million pixels per second through the rasteriser (%s pixels in %.1f s), about %.0f ns per pixel. Rough: the counter and the profile windows are not identical." % (int(pix[-1]) / (rn / 1e9) / 1e6, pix[-1], rn / 1e9, rn / float(max(int(pix[-1]), 1))))
        for scope in ("present", "clear", "scan_excluding_methods_ns"):
            if scope in last and scope != "scan_excluding_methods_ns":
                p("- Derived: %s took %.2f s in total (%.1f ms per call)." % (scope, last[scope][1] / 1e9, last[scope][1] / 1e6 / max(last[scope][0], 1)))

    # ---- controller
    p("")
    p("## Controller")
    up = first(r"\[MH-PAD\] poller up:.*", log)
    ch = re.findall(r"\[MH-PAD\] buttons=0x([0-9a-f]+) analog=0x([0-9a-f]+) connected=(\d)", log)
    if up:
        p("- %s" % up)
        masks = sorted(set((b, a) for b, a, _ in ch))
        p("- Button-change events seen by the console: %d (%d distinct states)" % (len(ch), len(masks)))
        if not ch:
            p("- **No button change was seen: either nothing was pressed, or the console is not delivering the controller to the game.**")
    else:
        p("- No `[MH-PAD]` lines: the game never polled the controller, or the build has no pad logging.")
    ip = first(r"\[MH-INPUT\] XInputOpen.*", log)
    if ip:
        p("- %s" % ip)
    polls = re.findall(r"\[MH-INPUT\] poll #(\d+) delivered buttons=0x([0-9a-f]+) analog=0x([0-9a-f]+)", log)
    if polls:
        nz = [x for x in polls if int(x[1], 16) or int(x[2], 16)]
        p("- The game's own polls (XInputGetState) seen in the log: highest poll number %s; polls logged with a non-zero state handed to the game: %d" % (polls[-1][0], len(nz)))
        if len(ch) and not nz:
            p("- **The console recorded presses but every poll the game made returned an idle pad: the presses never reached a game poll.**")
        elif nz:
            p("- Polls logged (the log is sampled, not every poll) show presses handed to the game (first non-zero poll: #%s, buttons=0x%s analog=0x%s). If the menu did not move, the cause is more likely in how the title interprets the input than in delivery (UNVERIFIED)." % (nz[0][0], nz[0][1], nz[0][2]))
    elif up:
        p("- No `[MH-INPUT] poll` lines: the game never called XInputGetState, or the build has no poll logging.")

    # ---- watchdog / crash
    p("")
    p("## Watchdog, crash and exit")
    ticks = re.findall(r"\[MH-WD\] t=(\d+) lines=(\d+) (ok|STALE) mem=(\d+)MB/(\d+)MB focus=(\d)", log)
    if ticks:
        t = ticks[-1]
        stale = sum(1 for x in ticks if x[2] == "STALE")
        p("- Watchdog ticks: %d (last t=%s, %s log lines, memory %sMB of %sMB, focus %s); quiet ticks: %d" % (len(ticks), t[0], t[1], t[3], t[4], t[5], stale))
    elif wd.strip():
        p("- wd.txt on the SD card: %d lines (stream had no ticks)" % len(wd.strip().splitlines()))
    else:
        p("- No watchdog ticks seen (`MANHUNT_WD` off, or the process stopped before the first tick).")
    crashes = re.findall(r"\[MH-CRASH\][^\n]*", log)
    exits = re.findall(r"\[MH-EXIT\][^\n]*", log)
    if crashes:
        p("- **Crash lines:**")
        for c in crashes[:6]:
            p("  - `%s`" % c.strip())
        p("  - Resolve addresses with `aarch64-none-elf-addr2line -f -e game/build/switch/manhunt.elf <pc offset>`.")
    if exits:
        for e in exits[:3]:
            p("- `%s`" % e.strip())
    if not crashes and not exits:
        p("- No `[MH-CRASH]` or `[MH-EXIT]` line in the log.")
    for name, label in (("crash_reports.new", "New Atmosphere crash reports"), ("fatal_errors.new", "New Atmosphere fatal-error reports")):
        txt = read(os.path.join(run, name)).strip()
        p("- %s since the run started: %s" % (label, ("%d (%s)" % (len(txt.splitlines()), ", ".join(txt.splitlines()[:4]))) if txt else "none seen"))

    # ---- console reachability (freeze / reset detector)
    p("")
    p("## Console reachability (ping and FTP, sampled every ~2 s)")
    rows = []
    for line in ping.splitlines():
        m = re.match(r"(\d\d:\d\d:\d\d) ping=(up|DOWN) (ftp|-)", line.strip())
        if m:
            rows.append(m.groups())
    if rows:
        trans = []
        prev = None
        for t, pg, ft in rows:
            key = (pg, ft)
            if key != prev:
                trans.append((t, pg, ft))
                prev = key
        for t, pg, ft in trans[:14]:
            p("- %s ping=%s ftp=%s" % (t, pg, "open" if ft == "ftp" else "closed"))
        # The game window: Sphaira's FTP is closed while the game runs.
        idx = [i for i, r in enumerate(rows) if r[2] == "-"]
        if idx:
            lo, hi = idx[0], idx[-1]
            inside = rows[lo:hi + 1]
            down_in = sum(1 for r in inside if r[1] == "DOWN")
            down_out = sum(1 for i, r in enumerate(rows) if r[1] == "DOWN" and not (lo <= i <= hi))
            p("- Game window (FTP closed): %s to %s, about %d s." % (rows[lo][0], rows[hi][0], 2 * len(inside)))
            # longest consecutive run of DOWN samples inside the window
            longest = cur = 0
            for r in inside:
                cur = cur + 1 if r[1] == "DOWN" else 0
                longest = max(longest, cur)
            if longest >= 5:
                p("- **Ping was down for %d consecutive samples (about %d s) inside the game window: the console stopped answering for a long time (a freeze or reset), not just the process.**" % (longest, 2 * longest))
            elif down_in:
                p("- Ping dropped for %d sample(s) inside the game window, at most %d in a row (about %d s). Comparable to the short Wi-Fi drops seen with the console idle in Sphaira, so not consistent with a freeze (cause UNVERIFIED)." % (down_in, longest, 2 * longest))
            else:
                p("- Ping stayed up for the whole game window: the console itself kept running.")
            if down_out:
                p("- %d sample(s) with ping down outside the game window (Sphaira running): short Wi-Fi drops, not related to the game." % down_out)
        else:
            p("- FTP never closed: the game was probably never started during this capture.")
    else:
        p("- No ping log.")

    # ---- frames
    fdir = os.path.join(run, "frames")
    if os.path.isdir(fdir):
        fl = sorted(f for f in os.listdir(fdir) if f.lower().endswith((".bmp", ".png")))
        p("")
        p("## Frames")
        p("- %d file(s) in `frames/` (private: game-derived; look at them, do not commit them)" % len(fl))
        bm = sorted(f for f in os.listdir(fdir) if f.lower().endswith(".bmp"))
        rows = []
        for f in bm:
            try:
                rows.append((f, highlighted_row(os.path.join(fdir, f))))
            except (IndexError, ValueError):
                rows.append((f, None))      # a damaged frame must never stop the summary
        if rows and any(r is not None for _, r in rows):
            seq = ["?" if r is None else MENU_NAMES[r] for _, r in rows]
            p("- Highlighted main-menu entry per frame, in order (heuristic pixel check, UNVERIFIED; `?` = not recognised): %s" % " > ".join(seq))
            known = [r for _, r in rows if r is not None]
            if len(set(known)) > 1:
                p("- **The highlight moved between frames (heuristic, UNVERIFIED: an animation could also move it); if you pressed buttons at those times, the menu most likely reacted to input.**")
            else:
                p("- The highlighted entry never changed across the recognised frames.")

    p("")
    p("_Generated by scripts/hw-summarize.py. Facts only; hardware performance, audio and stability are UNVERIFIED unless a measured number appears above._")
    text = "\n".join(out) + "\n"
    with open(os.path.join(run, "summary.md"), "w") as f:
        f.write(text)
    sys.stdout.write(text)


if __name__ == "__main__":
    if len(sys.argv) != 2 or not os.path.isdir(sys.argv[1]):
        sys.stderr.write(__doc__)
        sys.exit(2)
    main(sys.argv[1])
