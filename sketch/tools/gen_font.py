#!/usr/bin/env python3
"""Генератор сглаженных шрифтов (4 бита на пиксель) с кириллицей для ПЛАТА-ОС."""
from PIL import Image, ImageDraw, ImageFont

REG = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

# (размер, имя, файл) — для интерфейса обычное начертание, для заголовков жирное
SIZES = [
    (12, "sm", REG),
    (16, "md", REG),
    (22, "lg", BOLD),
]

EXTRA = "°·—→№•«»…₽"
chars = [chr(c) for c in range(0x20, 0x7F)]
chars += [chr(c) for c in range(0x410, 0x450)]
chars += ["Ё", "ё"]
chars += list(EXTRA)
chars = sorted(set(chars), key=ord)


def gen(size, name, path):
    f = ImageFont.truetype(path, size)
    capTop = f.getbbox("H")[1]
    glyphs, pool = [], bytearray()

    for ch in chars:
        bbox = f.getbbox(ch)
        w = max(0, bbox[2] - bbox[0])
        h = max(0, bbox[3] - bbox[1])
        adv = round(f.getlength(ch))
        if w == 0 or h == 0:
            glyphs.append((ord(ch), 0, 0, adv, 0, 0))
            continue

        # рисуем в градациях серого → 4 бита на пиксель
        img = Image.new("L", (w + 2, h + 2), 0)
        ImageDraw.Draw(img).text((1 - bbox[0], 1 - bbox[1]), ch, font=f, fill=255)

        off = len(pool)
        for row in range(h):
            hi = lo = None
            for col in range(w):
                v = img.getpixel((col + 1, row + 1)) >> 4      # 0..15
                if col % 2 == 0:
                    hi = v
                else:
                    pool.append((hi << 4) | v)
            if w % 2:                                          # нечётная ширина
                pool.append((hi << 4) if hi is not None else 0)
        glyphs.append((ord(ch), w, h, adv, bbox[1] - capTop, off))

    assert len(pool) < 65535, f"{name}: пул {len(pool)} > 65535"
    out = [f"// {name}: {size}px, {len(glyphs)} символов, {len(pool)} Б (4 бита/пиксель)"]
    out.append(f"static const uint8_t FONT_{name.upper()}_BITS[] PROGMEM = {{")
    for i in range(0, len(pool), 24):
        out.append("  " + ",".join(str(b) for b in pool[i:i + 24]) + ",")
    out.append("};")
    out.append(f"static const UIGlyph FONT_{name.upper()}_GLYPHS[{len(glyphs)}] PROGMEM = {{")
    for cp, w, h, adv, yo, off in glyphs:
        out.append(f"  {{{cp},{w},{h},{adv},{yo},{off}}},")
    out.append("};")
    return "\n".join(out), len(pool), len(glyphs)


hdr = ["""// СГЕНЕРИРОВАНО tools/gen_font.py — не редактировать вручную
// 4 бита на пиксель: 16 уровней сглаживания
#pragma once
#include <Arduino.h>

struct UIGlyph { uint16_t cp; uint8_t w, h, adv; int8_t yoff; uint16_t off; };
"""]
total = 0
for size, name, path in SIZES:
    body, pool, n = gen(size, name, path)
    hdr.append(body)
    total += pool
    print(f"  {name}: {pool} Б, {n} глифов, {size}px {'жирный' if 'Bold' in path else 'обычный'}")

hdr.append("""
inline const UIGlyph *uiGlyph(const UIGlyph *tab, int n, uint16_t cp) {
  int lo = 0, hi = n - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    uint16_t v = pgm_read_word(&tab[mid].cp);
    if (v == cp) return &tab[mid];
    if (v < cp) lo = mid + 1; else hi = mid - 1;
  }
  return nullptr;
}
""")
open("/home/user/esp32-flasher/examples/board-os/ui_font.h", "w", encoding="utf-8").write("\n".join(hdr))
print(f"ВСЕГО: {total} Б → ui_font.h")
