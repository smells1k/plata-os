/*
 *  ui_core.h — ядро «ОС» для 2.8" TFT 240x320 (ILI9341) + тач
 *
 *  Дисплей: SPI  SCK=18  MOSI=23  CS=15  DC=2  RST=4  BL=32
 *  Тач:     SCK=18  MOSI=23  MISO=19  CS=33  IRQ=36
 *  Подсветка WS2812: D8 / IO5
 *
 *  Всё рисование — своим кодом поверх TFT_eSPI, шрифты с кириллицей.
 */
#pragma once
#include <Arduino.h>
#include <SPI.h>
#include "board_config.h"
#if !USE_USB_DISPLAY
#include <TFT_eSPI.h>
#endif
#include "ui_font.h"

/* ───────────── цвета (RGB565) ───────────── */
#define C_BG      0x0841   // фон, почти чёрный
#define C_PANEL   0x10A2   // панель
#define C_PANEL2  0x1904
#define C_LINE    0x2945
#define C_TXT     0xEF7D
#define C_DIM     0x8410
#define C_ACC     0x2D7F   // синий акцент
#define C_OK      0x3666   // зелёный
#define C_WARN    0xFD20
#define C_ERR     0xE8A4
#define C_WHITE   0xFFFF
#define C_BLACK   0x0000

#define SCR_W 240
#define SCR_H 320
#define STATUS_H 22
#define DOCK_H 0

/* ───────────── шрифты ───────────── */
enum { F_SM = 0, F_MD = 1, F_LG = 2 };

struct UIFont { const UIGlyph *g; const uint8_t *bits; int n; };

static const UIFont UIFONTS[3] = {
  { FONT_SM_GLYPHS, FONT_SM_BITS, (int)(sizeof(FONT_SM_GLYPHS) / sizeof(UIGlyph)) },
  { FONT_MD_GLYPHS, FONT_MD_BITS, (int)(sizeof(FONT_MD_GLYPHS) / sizeof(UIGlyph)) },
  { FONT_LG_GLYPHS, FONT_LG_BITS, (int)(sizeof(FONT_LG_GLYPHS) / sizeof(UIGlyph)) },
};

static const uint8_t FONT_H[3] = { 15, 20, 27 };
static const uint8_t FONT_BASE[3] = { 12, 16, 22 };

/* ───────────── UTF-8 ───────────── */
// читает один символ UTF-8, двигает указатель
static uint16_t utf8Next(const char *&p) {
  uint8_t c = (uint8_t)*p;
  if (!c) return 0;
  if (c < 0x80) { p++; return c; }
  if ((c & 0xE0) == 0xC0) { uint16_t v = ((c & 0x1F) << 6) | (p[1] & 0x3F); p += 2; return v; }
  if ((c & 0xF0) == 0xE0) {
    uint16_t v = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    p += 3; return v;
  }
  if ((c & 0xF8) == 0xF0) { p += 4; return '?'; }
  p++; return '?';
}

/* ───────────── дисплей ───────────── */
#if USE_USB_DISPLAY
/* «Экран по USB»: рисуем не в стекло, а шлём команды в окно на компьютере.
   Протокол — 2 байта на координату, 2 на цвет, всё little-endian. */
class USBTFT {
public:
  void init() {}
  void setRotation(uint8_t) {}
  void setTextPadding(int) {}

  inline static uint8_t frame = 0;      /* C++17: можно инициализировать в классе */
  static void newFrame() { frame++; }
  /* следующий fillScreen начнёт новый кадр — иначе очистка уезжает в старый
     и окно на ПК не может отличить её от мусора */
  static void frameAfterClear() { frame++; }
  static void op(uint8_t code) { Serial.write(0xA5); Serial.write(code); Serial.write(frame); }
  static void u16(uint16_t v) { Serial.write(v & 0xFF); Serial.write(v >> 8); }
  static void hdr(uint8_t code, int x, int y, int w, int h, uint16_t c) {
    op(code); u16(x); u16(y); u16(w); u16(h); u16(c);
  }

  void fillScreen(uint16_t c) { frameAfterClear(); op(0x01); u16(c); }
  void fillRect(int x, int y, int w, int h, uint16_t c) { if (w > 0 && h > 0) hdr(0x02, x, y, w, h, c); }
  void drawRect(int x, int y, int w, int h, uint16_t c) { if (w > 0 && h > 0) hdr(0x03, x, y, w, h, c); }
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
    if (w <= 0 || h <= 0) return;
    op(0x04); u16(x); u16(y); u16(w); u16(h); u16(r); u16(c);
  }
  void drawRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
    if (w <= 0 || h <= 0) return;
    op(0x05); u16(x); u16(y); u16(w); u16(h); u16(r); u16(c);
  }
  void drawPixel(int x, int y, uint16_t c) { op(0x06); u16(x); u16(y); u16(c); }
  void fillCircle(int x, int y, int r, uint16_t c) { op(0x07); u16(x); u16(y); u16(r); u16(c); }
  void drawCircle(int x, int y, int r, uint16_t c) { op(0x08); u16(x); u16(y); u16(r); u16(c); }
  void drawFastHLine(int x, int y, int w, uint16_t c) { if (w > 0) { op(0x09); u16(x); u16(y); u16(w); u16(c); } }
  void drawFastVLine(int x, int y, int h, uint16_t c) { if (h > 0) { op(0x0A); u16(x); u16(y); u16(h); u16(c); } }
  /* горизонтальная полоса пикселей — для быстрой отрисовки текста */
  void run(int x, int y, int len, uint16_t c) { if (len > 0) { op(0x0B); u16(x); u16(y); u16(len); u16(c); } }
  /* текст произвольного кегля — компьютер рисует его своим шрифтом */
  void drawLine(int x0, int y0, int x1, int y1, uint16_t c) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
      drawPixel(x0, y0, c);
      if (x0 == x1 && y0 == y1) break;
      int e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  }
  /* высота содержимого экрана — программа сделает прокрутку */
  void scrollH(uint16_t h) { op(0x0E); u16(h); }
  void bigText(int x, int y, uint16_t col, uint8_t px, const char *s, int right = 0) {
    uint8_t len = 0; const char *q = s;
    while (q[len] && len < 120) len++;
    op(0x0D); u16(x); u16(y); u16(col); u16(right); Serial.write(px); Serial.write(len);
    for (uint8_t i = 0; i < len; i++) Serial.write((uint8_t)s[i]);
  }
};

USBTFT tft;
#else
TFT_eSPI tft = TFT_eSPI();
#endif

static bool g_dirty = true;

static inline void scrFill(uint16_t c) { tft.fillScreen(c); }

/* ширина строки в пикселях */
static int textW(const char *s, uint8_t f) {
  const UIFont &fn = UIFONTS[f];
  int w = 0;
  const char *p = s;
  while (*p) {
    uint16_t cp = utf8Next(p);
    if (!cp) break;
    const UIGlyph *g = uiGlyph(fn.g, fn.n, cp);
    w += g ? g->adv : fn.g[0].adv;
  }
  return w;
}

/* Сглаживание: смешиваем цвет текста с фоном по 4-битной маске */
static uint16_t g_textBg = C_BG;

static inline uint16_t blend565(uint16_t bg, uint16_t fg, uint8_t a) {
  uint8_t br = (bg >> 11) & 0x1F, bgc = (bg >> 5) & 0x3F, bb = bg & 0x1F;
  uint8_t fr = (fg >> 11) & 0x1F, fgc = (fg >> 5) & 0x3F, fb = fg & 0x1F;
  uint8_t r = br + ((int)fr - (int)br) * a / 15;
  uint8_t g2 = bgc + ((int)fgc - (int)bgc) * a / 15;
  uint8_t b = bb + ((int)fb - (int)bb) * a / 15;
  return (r << 11) | (g2 << 5) | b;
}

/* рисует строку; возвращает ширину */
static int textDraw(int x, int y, const char *s, uint8_t f, uint16_t col, uint16_t bg = 0, bool opaque = false, int right = 0) {
  const UIFont &fn = UIFONTS[f];
  int cx = x;

#if USE_USB_DISPLAY
  /* по USB — отправляем строку целиком, компьютер рисует настоящим шрифтом */
  {
    uint8_t len = 0;
    const char *q = s;
    while (q[len] && len < 120) len++;
    Serial.write((uint8_t)0xA5);
    Serial.write((uint8_t)0x0C);
    Serial.write(USBTFT::frame);              /* номер кадра — как у всех команд */
    Serial.write(x & 0xFF); Serial.write(x >> 8);
    Serial.write(y & 0xFF); Serial.write(y >> 8);
    Serial.write(f);
    Serial.write(col & 0xFF); Serial.write(col >> 8);
    Serial.write(right & 0xFF); Serial.write(right >> 8);   /* правый край: 2 байта */
    Serial.write(len);
    for (uint8_t i = 0; i < len; i++) Serial.write((uint8_t)s[i]);
  }
  const char *p = s;
  while (*p) {
    uint16_t cp = utf8Next(p);
    if (!cp) break;
    const UIGlyph *g = uiGlyph(fn.g, fn.n, cp);
    cx += g ? g->adv : fn.g[0].adv;
  }
  (void)bg; (void)opaque;
  return cx - x;
#else
  const char *p = s;
  while (*p) {
    uint16_t cp = utf8Next(p);
    if (!cp) break;
    const UIGlyph *g = uiGlyph(fn.g, fn.n, cp);
    if (!g) continue;
    if (g->w) {
      const uint8_t *bitmap = fn.bits + g->off;
      int rowBytes = (g->w + 1) / 2;      /* 4 бита на пиксель */
      int gy = y + g->yoff;
      for (int row = 0; row < g->h; row++) {
        for (int col2 = 0; col2 < g->w; col2++) {
          uint8_t b = pgm_read_byte(bitmap + row * rowBytes + (col2 >> 1));
          uint8_t a = (col2 & 1) ? (b & 0x0F) : (b >> 4);
          if (a == 15) tft.drawPixel(cx + col2, gy + row, col);
          else if (a) tft.drawPixel(cx + col2, gy + row, blend565(g_textBg, col, a));
        }
      }
    }
    cx += g->adv;
  }
  return cx - x;
#endif
}

/* крупная надпись: по USB — настоящим шрифтом нужного кегля, на железе — своим */
static void headline(int x, int y, const char *s, uint16_t col, uint8_t px) {
#if USE_USB_DISPLAY
  tft.bigText(x, y, col, px, s);
#else
  textDraw(x, y, s, px > 18 ? F_LG : F_MD, col);
#endif
}

static void textCenter(int y, const char *s, uint8_t f, uint16_t col) {
  textDraw((SCR_W - textW(s, f)) / 2, y, s, f, col);
}

static void textRight(int xr, int y, const char *s, uint8_t f, uint16_t col) {
  textDraw(xr - textW(s, f), y, s, f, col);
}

/* обрезает строку до ширины, ставит «…» */
static String textClip(const String &s, int maxW, uint8_t f) {
  if (textW(s.c_str(), f) <= maxW) return s;
  String r = s;
  while (r.length() > 1 && textW((r + "…").c_str(), f) > maxW) r.remove(r.length() - 1);
  return r + "…";
}

/* ───────────── простые виджеты ───────────── */
static void box(int x, int y, int w, int h, uint16_t fill, uint16_t border = C_LINE, uint8_t r = 6) {
  if (r == 0) { tft.fillRect(x, y, w, h, fill); if (border != fill) tft.drawRect(x, y, w, h, border); return; }
  tft.fillRoundRect(x, y, w, h, r, fill);
  if (border != fill) tft.drawRoundRect(x, y, w, h, r, border);
}

static void btnDraw(int x, int y, int w, int h, const char *label, uint16_t bg = C_PANEL, uint16_t fg = C_TXT, uint8_t fs = F_MD) {
  box(x, y, w, h, bg, C_LINE, 6);
  textDraw(x + (w - textW(label, fs)) / 2, y + (h - FONT_H[fs]) / 2 + 1, label, fs, fg);
}

static void barDraw(int x, int y, int w, int h, int pct, uint16_t col = C_ACC) {
  box(x, y, w, h, C_PANEL2, C_LINE, 4);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  int fw = (w - 4) * pct / 100;
  if (fw > 2) tft.fillRoundRect(x + 2, y + 2, fw, h - 4, 3, col);
}

/* ───────────── тач ───────────── */
extern bool  touchRawRead(int16_t &rx, int16_t &ry);
extern int   T_RAW_X0, T_RAW_X1, T_RAW_Y0, T_RAW_Y1;   // калибровка

struct TouchPt { int x = 0, y = 0; bool ok = false; };

static int  g_usbTouchX = -1, g_usbTouchY = -1;

/* приём касаний из окна на компьютере: 'T' x(2) y(2) — нажатие, 'R' — отпускание */
static void usbPoll() {
  while (Serial.available()) {
    int c = Serial.read();
    if (c == 0x54) {
      uint8_t b[4]; int n = 0; uint32_t t0 = millis();
      while (n < 4 && millis() - t0 < 60) {
        if (Serial.available()) b[n++] = Serial.read();
      }
      if (n == 4) {
        g_usbTouchX = b[0] | (b[1] << 8);
        g_usbTouchY = b[2] | (b[3] << 8);
      }
    } else if (c == 0x52) {
      g_usbTouchX = -1;
    } else if (c == 0x43) {          /* время от компьютера */
      uint8_t b[6]; int n = 0; uint32_t t0 = millis();
      while (n < 6 && millis() - t0 < 80) {
        if (Serial.available()) b[n++] = Serial.read();
      }
      if (n == 6) {
        extern void pcTimeAccept(uint32_t epoch, int16_t tzMin);
        pcTimeAccept((uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24),
                     (int16_t)(b[4] | (b[5] << 8)));
      }
    }
  }
}

static TouchPt touchGet() {
  TouchPt t;
#if USE_USB_DISPLAY
  usbPoll();
  if (g_usbTouchX < 0) return t;
  t.x = g_usbTouchX; t.y = g_usbTouchY; t.ok = true;
  return t;
#else
  int16_t rx, ry;
  if (!touchRawRead(rx, ry)) return t;
  t.x = map(rx, T_RAW_X0, T_RAW_X1, 0, SCR_W - 1);
  t.y = map(ry, T_RAW_Y0, T_RAW_Y1, 0, SCR_H - 1);
  t.x = constrain(t.x, 0, SCR_W - 1);
  t.y = constrain(t.y, 0, SCR_H - 1);
  t.ok = true;
  return t;
#endif
}

/* ждёт касания и отпускания, возвращает точку нажатия */
static TouchPt touchWait() {
  TouchPt t;
  /* ждём нажатия */
  uint32_t t0 = millis();
  while (millis() - t0 < 60000) {
    t = touchGet();
    if (t.ok) break;
    delay(8);
  }
  if (!t.ok) return t;
  delay(40);
  /* ждём отпускания */
  uint32_t t1 = millis();
  while (millis() - t1 < 3000) {
    if (!touchGet().ok) break;
    delay(8);
  }
  return t;
}


/* кодирует codepoint в UTF-8 (для отрисовки одной клавиши) */
static void cpToUtf8(uint16_t cp, char *out) {
  if (cp < 0x80) { out[0] = cp; out[1] = 0; }
  else if (cp < 0x800) { out[0] = 0xC0 | (cp >> 6); out[1] = 0x80 | (cp & 0x3F); out[2] = 0; }
  else { out[0] = 0xE0 | (cp >> 12); out[1] = 0x80 | ((cp >> 6) & 0x3F); out[2] = 0x80 | (cp & 0x3F); out[3] = 0; }
}

/* один символ по центру кнопки */
static void glyphCenter(int cx, int cy, uint16_t cp, uint8_t f, uint16_t col) {
  char b[4]; cpToUtf8(cp, b);
  int w = textW(b, f);
  int y = cy - FONT_H[f] / 2;
  textDraw(cx - w / 2, y, b, f, col);
}

/* экран «ОС» */
typedef struct Screen_s {
  const char *title;
  void (*draw)();
  void (*touch)(TouchPt);
} Screen;
