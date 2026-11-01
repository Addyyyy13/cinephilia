#!/usr/bin/env python3
"""Runs Cinephilia in a real pseudo-terminal and captures what it draws.

A small VT100/xterm emulator interprets the escape sequences the program
writes (cursor movement, erasing, colours, alternate screen), so each
snapshot is the screen a person would actually see. Snapshots are saved as
plain text and, if Pillow is installed, as PNG images.

This is a development aid, used to inspect the interactive interface and to
check that the terminal is restored on exit. Usage:

    tests/screen_capture.py BINARY OUTDIR [--cols 80] [--rows 24] [--truecolor]
            [--script NAME] [--data FILE]
"""
import argparse
import fcntl
import os
import pty
import select
import signal
import struct
import sys
import termios
import time
import unicodedata

KEYS = {"UP": "\x1b[A", "DOWN": "\x1b[B", "LEFT": "\x1b[D", "RIGHT": "\x1b[C",
        "ENTER": "\r", "ESC": "\x1b", "PGDN": "\x1b[6~", "PGUP": "\x1b[5~", "CTRL_C": "\x03"}


def xterm256(n):
    if n < 16:
        base = [(0, 0, 0), (205, 0, 0), (0, 205, 0), (205, 205, 0), (0, 0, 238), (205, 0, 205),
                (0, 205, 205), (229, 229, 229), (127, 127, 127), (255, 0, 0), (0, 255, 0),
                (255, 255, 0), (92, 92, 255), (255, 0, 255), (0, 255, 255), (255, 255, 255)]
        return base[n]
    if n < 232:
        n -= 16
        steps = [0, 95, 135, 175, 215, 255]
        return (steps[n // 36], steps[(n // 6) % 6], steps[n % 6])
    v = 8 + (n - 232) * 10
    return (v, v, v)


def char_width(ch):
    if unicodedata.combining(ch):
        return 0
    return 2 if unicodedata.east_asian_width(ch) in ("W", "F") else 1


class Screen:
    DEFAULT_FG = (200, 200, 200)
    DEFAULT_BG = (0, 0, 0)

    def __init__(self, cols, rows):
        self.cols, self.rows = cols, rows
        self.alt = False
        self.cursor_visible = True
        self.reset_attrs()
        self.clear()
        self.r = self.c = 0
        self.buf = b""

    def reset_attrs(self):
        self.fg, self.bg = None, None
        self.bold = self.italic = self.underline = False

    def blank(self):
        return [" ", self.fg, self.bg, False, False, False]

    def clear(self):
        self.grid = [[[" ", None, self.bg if hasattr(self, "bg") else None, False, False, False]
                      for _ in range(self.cols)] for _ in range(self.rows)]

    def scroll(self):
        self.grid.pop(0)
        self.grid.append([[" ", None, self.bg, False, False, False] for _ in range(self.cols)])

    def put(self, ch):
        w = char_width(ch)
        if w == 0:
            return
        if self.c + w > self.cols:
            self.c = 0
            self.newline()
        cell = [ch, self.fg, self.bg, self.bold, self.italic, self.underline]
        self.grid[self.r][self.c] = cell
        if w == 2 and self.c + 1 < self.cols:
            self.grid[self.r][self.c + 1] = ["", self.fg, self.bg, False, False, False]
        self.c += w

    def newline(self):
        self.r += 1
        if self.r >= self.rows:
            self.r = self.rows - 1
            self.scroll()

    def erase_line(self, mode):
        rng = range(self.c, self.cols) if mode == 0 else range(0, self.c + 1) if mode == 1 else range(self.cols)
        for i in rng:
            if 0 <= i < self.cols:
                self.grid[self.r][i] = [" ", None, self.bg, False, False, False]

    def erase_display(self, mode):
        if mode == 2 or mode == 3:
            for row in range(self.rows):
                self.grid[row] = [[" ", None, self.bg, False, False, False] for _ in range(self.cols)]
            return
        if mode == 0:
            self.erase_line(0)
            for row in range(self.r + 1, self.rows):
                self.grid[row] = [[" ", None, self.bg, False, False, False] for _ in range(self.cols)]

    def sgr(self, params):
        if not params:
            params = [0]
        i = 0
        while i < len(params):
            p = params[i]
            if p == 0:
                self.reset_attrs()
            elif p == 1:
                self.bold = True
            elif p == 22:
                self.bold = False
            elif p == 3:
                self.italic = True
            elif p == 23:
                self.italic = False
            elif p == 4:
                self.underline = True
            elif p == 24:
                self.underline = False
            elif p == 39:
                self.fg = None
            elif p == 49:
                self.bg = None
            elif p in (38, 48) and i + 1 < len(params):
                if params[i + 1] == 2 and i + 4 < len(params):
                    col = tuple(params[i + 2:i + 5])
                    i += 4
                elif params[i + 1] == 5 and i + 2 < len(params):
                    col = xterm256(params[i + 2])
                    i += 2
                else:
                    col = None
                if p == 38:
                    self.fg = col
                else:
                    self.bg = col
            i += 1

    def feed(self, data):
        text = data.decode("utf-8", errors="replace")
        i = 0
        while i < len(text):
            ch = text[i]
            if ch == "\x1b" and i + 1 < len(text) and text[i + 1] == "[":
                j = i + 2
                while j < len(text) and not ("@" <= text[j] <= "~"):
                    j += 1
                if j >= len(text):
                    return
                body, final = text[i + 2:j], text[j]
                private = body.startswith("?")
                nums = [int(x) if x.isdigit() else 0 for x in body.lstrip("?").split(";")] if body.lstrip("?") else []
                if final == "H":
                    self.r = max(0, min(self.rows - 1, (nums[0] if nums else 1) - 1))
                    self.c = max(0, min(self.cols - 1, (nums[1] if len(nums) > 1 else 1) - 1))
                elif final == "J":
                    self.erase_display(nums[0] if nums else 0)
                elif final == "K":
                    self.erase_line(nums[0] if nums else 0)
                elif final == "m":
                    self.sgr(nums)
                elif final in "hl" and private:
                    for n in nums:
                        if n == 25:
                            self.cursor_visible = final == "h"
                        if n == 1049:
                            self.alt = final == "h"
                            if final == "h":
                                self.erase_display(2)
                i = j + 1
                continue
            if ch == "\x1b":
                i += 2
                continue
            if ch == "\r":
                self.c = 0
            elif ch == "\n":
                self.c = 0  # the program's tty has OPOST/ONLCR on
                self.newline()
            elif ch == "\b":
                self.c = max(0, self.c - 1)
            elif ch >= " ":
                self.put(ch)
            i += 1

    def text(self):
        return "\n".join("".join(cell[0] for cell in row).rstrip() for row in self.grid)

    def png(self, path, font_path="/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"):
        try:
            from PIL import Image, ImageDraw, ImageFont
        except ImportError:
            return False
        size = 16
        font = ImageFont.truetype(font_path, size)
        try:
            bold = ImageFont.truetype(font_path.replace(".ttf", "-Bold.ttf"), size)
        except OSError:
            bold = font
        cw, ch = 10, 21
        img = Image.new("RGB", (self.cols * cw + 24, self.rows * ch + 24), self.DEFAULT_BG)
        d = ImageDraw.Draw(img)
        for r, row in enumerate(self.grid):
            for c, (glyph, fg, bg, b, it, ul) in enumerate(row):
                x, y = 12 + c * cw, 12 + r * ch
                d.rectangle([x, y, x + cw - 1, y + ch - 1], fill=bg or self.DEFAULT_BG)
                if glyph and glyph != " ":
                    colour = fg or self.DEFAULT_FG
                    if glyph in "▀▄█▒":  # draw blocks exactly
                        if glyph == "█":
                            d.rectangle([x, y, x + cw - 1, y + ch - 1], fill=colour)
                        elif glyph == "▀":
                            d.rectangle([x, y, x + cw - 1, y + ch // 2 - 1], fill=colour)
                        elif glyph == "▄":
                            d.rectangle([x, y + ch // 2, x + cw - 1, y + ch - 1], fill=colour)
                        else:
                            for yy in range(y, y + ch, 2):
                                for xx in range(x + (yy // 2) % 2, x + cw, 2):
                                    d.point((xx, yy), fill=colour)
                    else:
                        d.text((x, y + 2), glyph, font=bold if b else font, fill=colour)
                    if ul:
                        d.line([x, y + ch - 3, x + cw - 1, y + ch - 3], fill=colour)
        img.save(path)
        return True


SCRIPTS = {
    # name: list of (keys, seconds to wait, snapshot name or None)
    "tour": [
        ("", 4.0, "01_title_card"),
        ("ENTER", 0.8, "02_archive_index"),
        ("DOWN DOWN", 0.4, "03_index_selection_moved"),
        ("UP UP ENTER", 0.8, "04_collection"),
        ("DOWN", 0.4, "05_collection_scrolled"),
        ("ENTER", 0.8, "06_catalogue_card"),
        ("q q 5", 0.8, "07_tonights_selection"),
        ("ENTER", 2.0, "08_tonight_we_watch"),
        ("q q 7", 0.8, "09_statistics"),
        ("q 6", 0.8, "10_cinema_journal"),
        ("q 3", 0.6, "11_search_prompt"),
        ("wong ENTER", 0.8, "12_search_results"),
        ("q 8", 0.8, "13_settings"),
        ("q 9", 2.0, "14_farewell"),
    ],
    "resize": [
        ("", 4.0, None),
        ("ENTER", 0.8, "01_index_80x24"),
        ("RESIZE:60x20", 0.8, "02_index_after_resize_60x20"),
        ("1", 0.8, "03_collection_60x20"),
        ("q q", 2.0, None),
    ],
    "ctrlc": [
        ("", 1.0, None),
        ("ENTER", 0.8, "01_before_ctrl_c"),
        ("CTRL_C", 1.0, None),
    ],
}


def set_size(fd, rows, cols):
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binary")
    ap.add_argument("outdir")
    ap.add_argument("--cols", type=int, default=80)
    ap.add_argument("--rows", type=int, default=24)
    ap.add_argument("--truecolor", action="store_true")
    ap.add_argument("--script", default="tour")
    ap.add_argument("--data", default=None)
    ap.add_argument("--extra", nargs="*", default=[])
    args = ap.parse_args()
    os.makedirs(args.outdir, exist_ok=True)

    env = dict(os.environ, TERM="xterm-256color", LANG="en_US.UTF-8", LC_ALL="en_US.UTF-8",
               CINEPHILIA_TODAY="2026-10-22", CINEPHILIA_SEED="7")
    env.pop("NO_COLOR", None)
    if args.truecolor:
        env["COLORTERM"] = "truecolor"
    else:
        env.pop("COLORTERM", None)
    cmd = [args.binary] + (["--data", args.data] if args.data else []) + args.extra

    pid, fd = pty.fork()
    if pid == 0:
        os.execve(args.binary, cmd, env)
    set_size(fd, args.rows, args.cols)
    size = [args.cols, args.rows]
    raw = bytearray()
    start = [0]  # where output for the current window size begins

    def render():
        # Replaying the whole stream at once avoids splitting escape
        # sequences across pty reads.
        s = Screen(size[0], size[1])
        s.feed(bytes(raw[start[0]:]))
        return s

    def pump(seconds):
        end = time.time() + seconds
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.05)
            if r:
                try:
                    data = os.read(fd, 65536)
                except OSError:
                    return False
                if not data:
                    return False
                raw.extend(data)
        return True

    alive = True
    for keys, wait, snap in SCRIPTS[args.script]:
        for k in keys.split():
            if k.startswith("RESIZE:"):
                cols, rows = map(int, k.split(":")[1].split("x"))
                set_size(fd, rows, cols)
                size[0], size[1] = cols, rows
                start[0] = len(raw)
                os.kill(pid, signal.SIGWINCH)
            else:
                os.write(fd, KEYS.get(k, k).encode())
            pump(0.12)
        alive = pump(wait) and alive
        if snap:
            screen = render()
            with open(os.path.join(args.outdir, snap + ".txt"), "w") as f:
                f.write(screen.text() + "\n")
            screen.png(os.path.join(args.outdir, snap + ".png"))
    pump(0.5)
    try:
        _, status = os.waitpid(pid, 0)
    except ChildProcessError:
        status = 0
    with open(os.path.join(args.outdir, "raw_output.bin"), "wb") as f:
        f.write(raw)
    screen = render()
    tail = bytes(raw[-200:])
    restored = b"\x1b[?25h" in tail and b"\x1b[?1049l" in tail
    code = os.WEXITSTATUS(status) if os.WIFEXITED(status) else -os.WTERMSIG(status)
    print(f"exit={code} terminal_restored={restored} alt_screen_left={not screen.alt} "
          f"cursor_visible={screen.cursor_visible}")


if __name__ == "__main__":
    main()
