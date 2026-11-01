#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ЭКРАН-ПЛАТЫ v4.0 — окно на компьютере вместо физического TFT.

Переписано с нуля. Особенности:
  • не сбрасывает плату при подключении (DTR/RTS гасятся)
  • понимает и правильную, и сломанную прошивку (авто-определение текста)
  • если данных нет — пишет это ПРЯМО НА ЭКРАНЕ, а не молчит чёрным
  • R — перерисовать, T — попросить плату обновить кадр, Ctrl+колесо — размер
  • длинные разделы (Дополнительно, Система, Игры) прокручиваются колесом

Запуск:
    python3 ЭКРАН-ПЛАТЫ.py
    python3 ЭКРАН-ПЛАТЫ.py /dev/ttyUSB0
"""

import os
import sys
import time
import queue
import urllib.request
import struct
import fcntl
import termios
import threading
import tkinter as tk
import tkinter.font as tkfont

SCR_W, SCR_H = 240, 320
BAUD = 460800
import platform
IS_WIN = platform.system() == "Windows"
VERSION = "4.1"
SYNC = 0xA5

# ── опкоды ─────────────────────────────────────────────────
CLEAR, RECT, FRAME, RRECT, RFRAME = 0x01, 0x02, 0x03, 0x04, 0x05
PIXEL, DISC, RING, HLINE, VLINE, RUN = 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B
TEXT, BTEXT, SCROLL = 0x0C, 0x0D, 0x0E
BATCH_ON, BATCH_OFF = 0x0F, 0x10

# полная длина пакета, включая SYNC и номер кадра
# полная длина пакета: SYNC(1) + опкод(1) + кадр(1) + данные
#   CLEAR: цвет(2)                    = 5
#   RECT : x,y,w,h,цвет (5×2)         = 13
#   RRECT: x,y,w,h,r,цвет (6×2)       = 15
#   DISC : x,y,r,цвет (4×2)           = 11
#   HLINE: x,y,len,цвет (4×2)         = 11
LEN = {
    CLEAR: 5, RECT: 13, FRAME: 13, RRECT: 15, RFRAME: 15,
    PIXEL: 9, DISC: 11, RING: 11, HLINE: 11, VLINE: 11, RUN: 11,
    SCROLL: 5, BATCH_ON: 0, BATCH_OFF: 0,
}

FONT_PX = {0: 12, 1: 16, 2: 22}


def _bottom(p):
    """До какого y дотягивается фигура."""
    op, g = p["op"], p.get("g") or []
    try:
        if op in (RECT, FRAME, RRECT, RFRAME):
            return g[1] + g[3]
        if op in (DISC, RING):
            return g[1] + g[2]
        if op == VLINE:
            return g[1] + g[2]
        if op in (PIXEL, HLINE, RUN):
            return g[1] + 1
        if op == TEXT:
            return p["y"] + FONT_PX.get(p["font"], 16)
        if op == BTEXT:
            return p["y"] + p["px"]
    except (IndexError, KeyError, TypeError):
        pass
    return 0


def _sane(op, d):
    """Похожи ли числа на настоящие координаты. Ловит рассинхрон потока."""
    def small(v, lim=400):
        return 0 <= v <= lim
    if op in (RECT, FRAME):
        return len(d) >= 4 and small(d[0], SCR_W) and small(d[1], 700) \
            and small(d[2], SCR_W) and small(d[3], 700)
    if op in (RRECT, RFRAME):
        return len(d) >= 5 and small(d[0], SCR_W) and small(d[1], 700) \
            and small(d[2], SCR_W) and small(d[3], 700) and d[4] <= 60
    if op in (PIXEL, DISC, RING, HLINE, VLINE, RUN):
        return len(d) >= 3 and small(d[0], SCR_W) and small(d[1], 700) \
            and small(d[2], 700)
    if op == SCROLL:
        return len(d) >= 1 and 100 <= d[0] <= 4000
    return True
BOLD = {2}


def to_rgb(c):
    return "#%02x%02x%02x" % (
        ((c >> 11) & 0x1F) * 255 // 31,
        ((c >> 5) & 0x3F) * 255 // 63,
        (c & 0x1F) * 255 // 31,
    )


def find_port():
    try:
        names = sorted(os.listdir("/dev"))
    except OSError:
        return None
    for p in names:
        if p.startswith("ttyUSB") or p.startswith("ttyACM"):
            return "/dev/" + p
    return None


# ═══════════════ ПОРТ ═══════════════
TIOCMBIC = 0x5417
TIOCM_DTR = 0x002
TIOCM_RTS = 0x004


class Port:
    def __init__(self, path):
        self.path = path
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        a = termios.tcgetattr(self.fd)
        sp = getattr(termios, "B%d" % BAUD)
        a[0] = 0
        a[1] = 0
        a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        a[3] = 0
        a[4] = a[5] = sp
        termios.tcsetattr(self.fd, termios.TCSANOW, a)
        # ВАЖНО: не дёргать DTR/RTS — иначе плата уйдёт в сброс и замолчит
        try:
            fcntl.ioctl(self.fd, TIOCMBIC, struct.pack("I", TIOCM_DTR | TIOCM_RTS))
        except OSError:
            pass
        termios.tcflush(self.fd, termios.TCIOFLUSH)

        self.buf = bytearray()
        self.frames = queue.Queue()
        self.pc_cmds = queue.Queue()   # команды компьютеру, пришедшие по проводу
        self.raw_head = b""          # первые байты для диагностики
        self.bytes_in = 0
        self.bad = 0                 # пакетов с неверными числами
        self.dups = 0                # повторов одного кадра
        self.last_f = -1
        self.alive = True
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        while self.alive:
            try:
                chunk = os.read(self.fd, 65536)
            except BlockingIOError:
                time.sleep(0.004)
                continue
            except OSError:
                break
            if chunk:
                self.bytes_in += len(chunk)
                if len(self.raw_head) < 24:
                    self.raw_head += chunk[:24 - len(self.raw_head)]
                self.buf += chunk
                self._decode()
            else:
                time.sleep(0.004)

    # ── разбор ──
    def _decode(self):
        b = self.buf
        while b:
            # команда компьютеру: 'P' длина байты — без SYNC, это текст
            if b[0] == 0x50 and len(b) >= 2:
                n = b[1]
                if n == 0 or n > 32:
                    del b[0]
                    continue
                if len(b) < 2 + n:
                    return                      # ждём остаток
                cmd = bytes(b[2:2 + n]).decode("utf-8", "replace")
                self.pc_cmds.put(cmd)
                del b[:2 + n]
                continue
            if b[0] != SYNC:
                del b[0]
                continue
            if len(b) < 3:
                return
            op, frame = b[1], b[2]

            if op in (TEXT, BTEXT):
                pkt = self._text_packet(b) if op == TEXT else self._big_text_packet(b)
                if pkt is None:            # пакет ещё не весь пришёл — ЖДЁМ
                    return
                if pkt is False:
                    self.bad += 1
                    del b[0]               # совсем не похоже — пропускаем байт
                    continue
                if pkt["frame"] != self.last_f:
                    self.last_f = pkt["frame"]
                self.frames.put(pkt)
                del b[:pkt["used"]]
                continue

            n = LEN.get(op)
            if n is None:
                del b[0]
                continue
            if n == 0:                     # партия: без данных, просто флаг
                self.frames.put({"op": op, "frame": frame, "g": []})
                del b[:3]
                continue
            if len(b) < n:
                return
            data = [b[i] | (b[i + 1] << 8) for i in range(3, n - 1, 2)]
            # отсев рассинхрона: координаты и размеры не выходят за 400
            if not _sane(op, data):
                self.bad += 1
                del b[0]
                continue
            if frame == self.last_f:
                self.dups += 1
            else:
                self.last_f = frame
            pkt = {"op": op, "frame": frame, "g": data}
            if op == SCROLL:
                pkt["h"] = data[0] if data else SCR_H
            self.frames.put(pkt)
            del b[:n]

    def _text_packet(self, b):
        """Текст: SYNC | 0x0C | кадр | x2 y2 font col2 right2 len | строка

        Пакет приходит кусками, поэтому «не хватает байт» и «мусор» надо
        различать строго: иначе часть подписей навсегда теряется.
        """
        if len(b) < 13:
            return None
        if not self._frame_ok(b[2]):      # номер кадра не наш — поток сдвинут
            return False
        ln = b[12]
        x = b[3] | (b[4] << 8)
        y = b[5] | (b[6] << 8)
        f = b[7]
        col = b[8] | (b[9] << 8)
        right = b[10] | (b[11] << 8)
        if x > SCR_W or y > SCR_H or f > 2 or right > SCR_W:
            return False
        last = b[13 + ln - 1] if len(b) >= 13 + ln else 0
        if last == SYNC:                  # строка кончается началом команды — рано
            return False
        if len(b) < 13 + ln:
            return None                   # ждём остаток строки
        return {"op": TEXT, "frame": b[2], "x": x, "y": y, "font": f,
                "col": col, "right": right,
                "text": bytes(b[13:13 + ln]).decode("utf-8", "replace"),
                "used": 13 + ln}

    def _frame_ok(self, f):
        """Номер кадра растёт маленькими шагами. Большой скачок = мы не там."""
        if self.last_f < 0:
            return True                   # первый пакет — принимаем как есть
        d = (f - self.last_f) & 0xFF
        return d <= 8

    def _big_text_packet(self, b):
        """Крупный текст: SYNC | 0x0D | кадр | x2 | y2 | цвет2 | right2 | px | len | строка"""
        if len(b) < 13:
            return None
        if not self._frame_ok(b[2]):
            return False
        x = b[3] | (b[4] << 8)
        y = b[5] | (b[6] << 8)
        col = b[7] | (b[8] << 8)
        right = b[9] | (b[10] << 8)
        px = b[11]
        ln = b[12]
        if x > SCR_W or y > SCR_H or px == 0 or px > 80 or right > SCR_W:
            return False
        if len(b) >= 13 + ln and b[13 + ln - 1] == SYNC:
            return False
        if len(b) < 13 + ln:
            return None
        return {"op": BTEXT, "frame": b[2], "x": x, "y": y, "col": col,
                "right": right, "px": px,
                "text": bytes(b[13:13 + ln]).decode("utf-8", "replace"),
                "used": 13 + ln}

    def send_state(self, body):
        """Отправляем плате своё состояние. Формат: 'S' len строка."""
        try:
            raw = body.encode("utf-8")[:200]
            self.send(b"S" + bytes([len(raw)]) + raw)
        except OSError:
            pass

    def send(self, data):
        try:
            os.write(self.fd, data)
        except OSError:
            pass

    def close(self):
        self.alive = False
        try:
            os.close(self.fd)
        except OSError:
            pass


# ═══════════════ ОКНО ═══════════════
def pc_state():
    """Громкость, mute и имя ПК — для отправки на плату."""
    import subprocess
    vol, muted, host = 50, False, ""
    try:
        host = __import__("socket").gethostname()
    except OSError:
        pass
    try:
        out = subprocess.run(["pactl", "get-sink-volume", "@DEFAULT_SINK@"],
                             capture_output=True, text=True, timeout=2).stdout
        for part in out.split():
            if part.endswith("%"):
                vol = int(part.rstrip("%"))
                break
    except Exception:
        pass
    try:
        out = subprocess.run(["pactl", "get-sink-mute", "@DEFAULT_SINK@"],
                             capture_output=True, text=True, timeout=2).stdout
        muted = "yes" in out.lower()
    except Exception:
        pass
    return vol, muted, host[:20]


class Screen(tk.Tk):
    def __init__(self, port):
        super().__init__()
        self.port = port
        self.title("Экран платы — v" + VERSION)
        self.configure(bg="#0d1117")
        self.resizable(False, False)

        self.scale = self.pick_scale()
        self.pkts = []            # все фигуры с последней очистки
        self.done = 0             # сколько из них уже нарисовано
        self.content_h = SCR_H
        self.board_h = SCR_H        # что сообщила плата
        self.max_y = SCR_H          # самый низ нарисованного
        self.scroll = 0
        self.frozen = False
        self.frozen_at = 0.0
        self.drawn = 0
        self.got_any = False
        self.touch_down = False
        self.fon = {}
        self.pc_done = 0
        self.state_at = 0.0
        self.last_fix = 0.0        # когда последний раз лечили рассинхрон
        self.fixes = 0

        self.canvas = tk.Canvas(self, bg="#000000", highlightthickness=0, cursor="hand2")
        self.canvas.pack(padx=8, pady=(8, 2))
        self.status = tk.Label(self, text="…", bg="#0d1117", fg="#8b949e",
                               font=("DejaVu Sans", 8), wraplength=460, justify="left")
        self.status.pack(pady=(0, 6))
        self.apply_size()

        self.canvas.bind("<Button-1>", self.press)
        self.canvas.bind("<ButtonRelease-1>", self.release)
        for w in (self, self.canvas):
            w.bind("<MouseWheel>", self.wheel)
            w.bind("<Button-4>", self.wheel)
            w.bind("<Button-5>", self.wheel)
        self.bind("<Control-plus>", lambda e: self.zoom(1))
        self.bind("<Control-minus>", lambda e: self.zoom(-1))
        self.bind("<F5>", lambda e: self.redraw())
        self.bind("<KeyPress-r>", lambda e: self.redraw())
        self.bind("<KeyPress-t>", lambda e: self.refresh())
        self.bind("<KeyPress-u>", lambda e: self.ota_ask())
        self.bind("<KeyPress-U>", lambda e: self.ota_ask())
        self.bind("<KeyPress-f>", lambda e: self.fit())
        self.bind("<KeyPress-F>", lambda e: self.fit())
        self.focus_set()

        self.after(12, self.pump)
        self.after(500, self.tick)
        self.after(900, self.ask_redraw)

    # ── размеры ──
    def pick_scale(self):
        try:
            h = self.winfo_screenheight()
        except tk.TclError:
            h = 900
        s = (h - 200) / SCR_H
        return max(0.75, min(2.0, round(s * 4) / 4))

    def apply_size(self):
        w, h = int(SCR_W * self.scale), int(SCR_H * self.scale)
        self.canvas.config(width=w, height=h)
        sw, sh = self.winfo_screenwidth(), self.winfo_screenheight()
        self.geometry("%dx%d+%d+%d" % (w + 16, h + 70,
                                       max(0, (sw - w - 20) // 2),
                                       max(0, (sh - h - 90) // 2)))

    def zoom(self, d):
        self.scale = max(0.75, min(3.0, self.scale + 0.25 * d))
        self.apply_size()
        self.redraw()

    def fit(self):
        self.scale = self.pick_scale()
        self.apply_size()
        self.redraw()

    def font(self, px, bold=False):
        k = (int(px), bold)
        if k not in self.fon:
            self.fon[k] = tkfont.Font(family="DejaVu Sans", size=-max(6, int(px)),
                                      weight="bold" if bold else "normal")
        return self.fon[k]

    # ── касания ──
    def press(self, e):
        x = min(max(int(e.x / self.scale), 0), SCR_W - 1)
        y = min(max(int(e.y / self.scale) + self.scroll, 0), 8000)
        self.port.send(b"\x54" + x.to_bytes(2, "little") + y.to_bytes(2, "little"))
        self.touch_down = True

    def release(self, _e):
        if self.touch_down:
            self.port.send(b"\x52")
            self.touch_down = False

    def wheel(self, e):
        raw = int(getattr(e, "delta", 0)) or (1 if getattr(e, "num", 0) == 4 else -1)
        if e.state & 0x0004:
            self.zoom(1 if raw > 0 else -1)
            return
        if self.content_h <= SCR_H:
            return
        self.scroll = int(max(0, min(self.content_h - SCR_H,
                                     self.scroll - raw * 12 / max(self.scale, 0.1))))
        self.canvas.delete("all")
        self.done = 0
        self.render(self.pkts)
        self.done = len(self.pkts)
        self.mark()
        self.canvas.tag_raise("ui")

    # ── приём ──
    def pump(self):
        n = 0                  # получено пакетов
        cleared = False
        while True:
            try:
                p = self.port.frames.get_nowait()
            except queue.Empty:
                break
            n += 1
            if n > 20000:
                break
            self.got_any = True            # любая команда = связь есть
            op = p["op"]
            if op == BATCH_ON:
                self.frozen = True
                self.frozen_at = time.time()
                continue
            if op == BATCH_OFF:
                self.frozen = False
                continue
            if op == CLEAR:
                if self.frozen:
                    continue          # заставка идёт — фон не трогаем
                cleared = True
                self.pkts = []
                self.done = 0
                self.board_h = SCR_H
                self.max_y = SCR_H
            elif op == SCROLL:
                self.board_h = max(SCR_H, min(4000, p.get("h", SCR_H)))
                self.pkts.append(p)
                self.done = len(self.pkts)
            else:
                self.pkts.append(p)
                b = _bottom(p)
                if b > self.max_y:
                    self.max_y = min(4000, b)

        if n:
            self.drawn += n
            if getattr(self, "frozen", False) and time.time() - getattr(self, "frozen_at", 0) > 3:
                self.frozen = False        # партия оборвалась — не залипаем
            # высота содержимого: что сказала плата ИЛИ что реально нарисовано
            self.content_h = max(SCR_H, self.board_h, self.max_y + 6)
            s_reg = self.scroll
            if cleared or self.scroll > max(0, self.content_h - SCR_H):
                self.scroll = max(0, min(self.scroll, self.content_h - SCR_H))
            # если сдвиг прокрутки поменялся — перерисовываем всё
            if cleared or self.scroll != s_reg:
                self.canvas.delete("all")
                self.done = 0
            self.render(self.pkts[self.done:])
            self.done = len(self.pkts)
            self.mark()
            self.canvas.tag_raise("ui")
            self.hide_notice()
        self.run_pc_cmds()
        if time.time() - self.state_at > 2.0:
            self.state_at = time.time()
            self.push_state()
        self.after(12, self.pump)

    def render(self, items):
        for p in items:
            self.paint(p)

    def state_str(self):
        vol, muted, host = pc_state()
        return "vol=%d muted=%d host=%s" % (vol, 1 if muted else 0, host)

    def push_state(self):
        """Плата просит состояние (или пора обновить) — отвечаем."""
        try:
            self.port.send_state(self.state_str())
        except Exception:
            pass

    def run_pc_cmds(self):
        """Выполняем команды, которые плата прислала по кабелю."""
        while True:
            try:
                cmd = self.port.pc_cmds.get_nowait()
            except queue.Empty:
                return
            self.pc_done += 1
            self.exec_pc(cmd)

    def exec_pc(self, cmd):
        """lock / sleep / shutdown / reboot / vol_up / mute / wol MAC."""
        cmd = cmd.strip()
        self.flash("→ " + cmd)
        try:
            import subprocess
        except ImportError:
            return
        table = {
            "lock":      ["loginctl", "lock-session"],
            "sleep":     ["systemctl", "suspend"],
            "hibernate": ["systemctl", "hibernate"],
            "shutdown":  ["systemctl", "poweroff"],
            "reboot":    ["systemctl", "reboot"],
            "vol_up":    ["pactl", "set-sink-volume", "@DEFAULT_SINK@", "+5%"],
            "vol_down":  ["pactl", "set-sink-volume", "@DEFAULT_SINK@", "-5%"],
            "mute":      ["pactl", "set-sink-mute", "@DEFAULT_SINK@", "toggle"],
            "playpause": ["playerctl", "play-pause"],
            "next":      ["playerctl", "next"],
            "prev":      ["playerctl", "previous"],
        }
        if IS_WIN:
            table = {
                "lock":     ["rundll32.exe", "user32.dll,LockWorkStation"],
                "sleep":    ["rundll32.exe", "powrprof.dll,SetSuspendState", "0,1,0"],
                "shutdown": ["shutdown", "/s", "/f", "/t", "0"],
                "reboot":   ["shutdown", "/r", "/f", "/t", "0"],
            }
        if cmd in ("ping", "status", "host"):
            self.push_state()
            self.flash("состояние ПК → плате")
            return
        if cmd.startswith("wol"):
            mac = cmd[3:].strip(" ?=&:")
            self.flash("WoL " + mac)
            self.send_wol(mac)
            return
        args = table.get(cmd)
        if not args:
            self.flash("не знаю: " + cmd)
            return
        try:
            subprocess.Popen(args, stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL)
        except Exception as e:
            self.flash("ошибка: " + type(e).__name__)

    def send_wol(self, mac):
        import socket as sk
        mac = mac.replace(":", "").replace("-", "")
        if len(mac) != 12:
            return
        try:
            packet = b"\xff" * 6 + bytes.fromhex(mac) * 16
        except ValueError:
            return
        for addr in ("255.255.255.255",):
            try:
                u = sk.socket(sk.AF_INET, sk.SOCK_DGRAM)
                u.setsockopt(sk.SOL_SOCKET, sk.SO_BROADCAST, 1)
                u.sendto(packet, (addr, 9))
                u.close()
            except OSError:
                pass

    # ── обновление по воздуху ──
    def ota_ask(self):
        """U — прошить плату по Wi-Fi, без провода."""
        from tkinter import filedialog, messagebox
        path = filedialog.askopenfilename(
            title="Прошивка для платы (по Wi-Fi)",
            filetypes=[("Прошивка платы", "*.bin"), ("Все файлы", "*")])
        if not path:
            return
        host = filedialog.askstring(
            "Адрес платы", "IP или имя платы в сети:", initialvalue="plata-os.local")
        if not host:
            return
        if messagebox.askyesno("Обновить плату",
                               "Прошить %s\nна плату %s ?\n\nПлата перезапустится."
                               % (os.path.basename(path), host)):
            self.ota_flash(path, host)

    def ota_flash(self, path, host):
        """Отправляем файл на плату: она сама себя перепрошьёт."""
        import threading

        def work():
            try:
                self.flash("обновление: отправляю…")
                data = open(path, "rb").read()
                boundary = "----plataOS"
                head = ("--%s\r\nContent-Disposition: form-data; name=\"f\"; "
                        "filename=\"%s\"\r\nContent-Type: application/octet-stream"
                        "\r\n\r\n" % (boundary, os.path.basename(path))).encode()
                tail = ("\r\n--%s--\r\n" % boundary).encode()
                body = head + data + tail
                req = urllib.request.Request(
                    "http://%s/update" % host, data=body,
                    headers={"Content-Type": "multipart/form-data; boundary=" + boundary})
                with urllib.request.urlopen(req, timeout=180) as r:
                    ok = r.status == 200
                self.after(0, lambda: self.flash(
                    "плата обновлена — перезапускается" if ok else "плата ответила отказом"))
            except Exception as e:
                err = type(e).__name__
                self.after(0, lambda: self.flash("обновление не вышло: " + err))

        threading.Thread(target=work, daemon=True).start()

    def flash(self, text):
        """Короткая строка внизу: что сейчас выполняется."""
        self.canvas.delete("flash")
        self.canvas.create_text(6 * self.scale, (SCR_H - 12) * self.scale,
                                text=text, anchor="w", tags="flash",
                                fill="#7ee787",
                                font=("DejaVu Sans", -max(7, int(10 * self.scale))))

    def mark(self):
        """Полоса прокрутки справа: видно, где ты и сколько ещё внизу."""
        self.canvas.delete("ui")
        if not self.pkts:
            return
        s = self.scale
        c = self.canvas
        x0, x1 = SCR_W - 3, SCR_W - 1
        top, bot = 4, SCR_H - 4
        if self.content_h <= SCR_H:
            return                       # всё влезло — полоса не нужна
        span = bot - top
        kh = max(24, int(span * SCR_H / self.content_h))
        ky = top + int((span - kh) * self.scroll / max(1, self.content_h - SCR_H))

        # фон дорожки
        c.create_rectangle((x0 - 1) * s, top * s, x1 * s, bot * s,
                           fill="#21262d", outline="", tags="ui")
        # ползунок
        c.create_rectangle(x0 * s, ky * s, x1 * s, (ky + kh) * s,
                           fill="#58a6ff", outline="", tags="ui")
        # подсказки: есть ли ещё выше / ниже
        if self.scroll > 0:
            c.create_text((SCR_W - 6) * s, 6 * s, text="▲", fill="#8b949e",
                          font=("DejaVu Sans", max(7, int(6 * s))), anchor="n", tags="ui")
        if self.scroll < self.content_h - SCR_H:
            c.create_text((SCR_W - 6) * s, (SCR_H - 6) * s, text="▼", fill="#8b949e",
                          font=("DejaVu Sans", max(7, int(6 * s))), anchor="s", tags="ui")

    def ask_redraw(self):
        """Просим плату нарисовать текущий экран заново."""
        self.port.send(b"D")
        self.canvas.delete("all")
        self.pkts = []
        self.done = 0
        self.content_h = SCR_H
        self.board_h = SCR_H
        self.max_y = SCR_H
        self.scroll = 0
        self.got_any = False
        self.port.bad = 0
        self.port.dups = 0
        self.after(1500, self.check_after_redraw)

    def check_after_redraw(self):
        if not self.port.bytes_in:
            return          # тик сам напишет «плата молчит»
        if not self.pkts:
            self.notice(["Плата идёт, но экран не нарисовался",
                         "нажми R — попросить перерисовать"])

    def redraw(self):
        """Перерисовать всё, что уже известно (без ожидания платы)."""
        self.canvas.delete("all")
        self.done = 0
        self.render(self.pkts)
        self.done = len(self.pkts)
        self.mark()
        self.canvas.tag_raise("ui")

    def refresh(self):
        """Попросить плату нарисовать экран заново — на случай, если разбор уехал."""
        self.after(1, self.ask_redraw)

    # ── рисование ──
    def paint(self, p):
        try:
            self._paint(p)
        except Exception:
            pass           # одна кривая фигура не должна ломать картинку

    def _paint(self, p):
        c = self.canvas
        s = self.scale
        sy = self.scroll
        op = p["op"]

        def X(v): return v * s
        def Y(v): return (v - sy) * s

        if op in (CLEAR, SCROLL, BATCH_ON, BATCH_OFF):
            return

        if op in (RECT, FRAME, RRECT, RFRAME):
            g = p["g"]
            if len(g) < 5:
                return
            x, y, w, h = g[0], g[1], g[2], g[3]
            col = to_rgb(g[4] if op in (RECT, FRAME) else g[5])
            fill = col if op in (RECT, RRECT) else ""
            c.create_rectangle(X(x), Y(y), X(x + w), Y(y + h),
                               fill=fill, outline=col, width=1)
            return

        if op == PIXEL:
            g = p["g"]
            col = to_rgb(g[2])
            c.create_rectangle(X(g[0]), Y(g[1]), X(g[0] + 1), Y(g[1] + 1),
                               fill=col, outline=col)
            return

        if op in (DISC, RING):
            g = p["g"]
            x, y, r, col = g[0], g[1], g[2], to_rgb(g[3])
            c.create_oval(X(x - r), Y(y - r), X(x + r), Y(y + r),
                          fill=col if op == DISC else "", outline=col)
            return

        if op == HLINE:
            g = p["g"]
            col = to_rgb(g[3])
            c.create_rectangle(X(g[0]), Y(g[1]), X(g[0] + g[2]), Y(g[1] + 1),
                               fill=col, outline=col)
            return

        if op == VLINE:
            g = p["g"]
            col = to_rgb(g[3])
            c.create_rectangle(X(g[0]), Y(g[1]), X(g[0] + 1), Y(g[1] + g[2]),
                               fill=col, outline=col)
            return

        if op == RUN:
            g = p["g"]
            col = to_rgb(g[3])
            c.create_rectangle(X(g[0]), Y(g[1]), X(g[0] + g[2]), Y(g[1] + 1),
                               fill=col, outline=col)
            return

        if op == TEXT:
            self.paint_text(p)
            return

        if op == BTEXT:
            x, y, col, right, px, txt = p["x"], p["y"], p["col"], p["right"], p["px"], p["text"]
            f = self.font(px * s, True)
            txt = self.cut(txt, f, min((right - x) if right > x else 999, SCR_W - x - 2) * s)
            c.create_text(X(x), Y(y) + px * s * 0.38, text=txt, anchor="w",
                          font=f, fill=to_rgb(col), tags="txt")
            return

    def paint_text(self, p):
        c = self.canvas
        s = self.scale
        px = FONT_PX.get(p["font"], 16) * s
        f = self.font(px, p["font"] in BOLD)
        room = (p["right"] - p["x"]) if p["right"] > p["x"] else SCR_W - p["x"] - 2
        room = min(room, SCR_W - p["x"] - 2) * s
        txt = self.cut(p["text"], f, room)
        c.create_text(p["x"] * s, (p["y"] - self.scroll) * s + px * 0.36,
                      text=txt, anchor="w", font=f, fill=to_rgb(p["col"]), tags="txt")

    def cut(self, txt, f, maxpx):
        if maxpx <= 0:
            return ""
        if f.measure(txt) <= maxpx:
            return txt
        while txt and f.measure(txt + "…") > maxpx:
            txt = txt[:-1]
        return txt + "…"

    # ── подсказки на экране ──
    def notice(self, lines, color="#f85149"):
        self.canvas.delete("notice")
        y = SCR_H / 2 - 30 * len(lines)
        for i, t in enumerate(lines):
            self.canvas.create_text(SCR_W * self.scale / 2, self.scale * (y + i * 26),
                                    text=t, fill=color, tags="notice",
                                    font=("DejaVu Sans", -max(7, int(13 * self.scale))),
                                    width=int(SCR_W * self.scale - 30))

    def hide_notice(self):
        self.canvas.delete("notice")

    def tick(self):
        if not self.port.bytes_in:
            self.notice(["Плата молчит",
                         "нет ни одного байта по USB",
                         "проверь кабель и прошивку"])
        elif not self.pkts:
            hx = " ".join("%02X" % x for x in self.port.raw_head[:12])
            self.notice(["Жду картинку от платы…",
                         "первые байты: " + hx,
                         "клавиша T — попросить перерисовать"])
        elif self.port.bad >= 4 and time.time() - self.last_fix > 3.0:
            # поток разъехался — один раз просим плату нарисовать заново
            self.last_fix = time.time()
            self.fixes += 1
            self.refresh()
        txt = "порт %s · %.2f× · %d байт · %d фигур" % (
            self.port.path, self.scale, self.port.bytes_in, len(self.pkts))
        if self.content_h > SCR_H:
            txt += " · вниз ещё %d px · колесо" % max(0, self.content_h - SCR_H - self.scroll)
        if self.pc_done:
            txt += " · ПК: %d ком." % self.pc_done
        if self.port.bad:
            txt += " · сбой %d" % self.port.bad
        if self.fixes:
            txt += " · лечил %d" % self.fixes
        self.status.config(text=txt)
        self.after(600, self.tick)


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else find_port()
    if not path:
        print("Плата не найдена. Воткни USB или укажи порт:")
        print("   python3 ЭКРАН-ПЛАТЫ.py /dev/ttyUSB0")
        return 1
    print("ЭКРАН-ПЛАТЫ v%s · порт %s · %d бод" % (VERSION, path, BAUD))
    port = Port(path)
    app = Screen(port)
    app.t0 = time.time()
    try:
        app.mainloop()
    finally:
        port.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
