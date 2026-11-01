/* ══════════ ЭКРАН ЗАПУСКА ══════════ */
/* Чистая заставка: линия обводит рамку, в логотипе загорается диод,
   снизу идёт полоса. Никакой спирали из точек — она выглядела грязно. */

/* отрезок по параметру 0..1 вдоль рамки */
static void bootTrace(int x, int y, int w, int h, int total, int done, uint16_t col) {
  int per = 2 * (w + h);                     /* периметр */
  int upto = per * done / total;
  int left = upto;
  /* верх */
  int seg = w;
  if (left > 0) { int n = left < seg ? left : seg; tft.fillRect(x, y, n, 2, col); left -= n; }
  /* право */
  if (left > 0) { seg = h; int n = left < seg ? left : seg; tft.fillRect(x + w - 2, y, 2, n, col); left -= n; }
  /* низ */
  if (left > 0) { seg = w; int n = left < seg ? left : seg; tft.fillRect(x + w - n, y + h - 2, n, 2, col); left -= n; }
  /* лево */
  if (left > 0) { seg = h; int n = left < seg ? left : seg; tft.fillRect(x, y + h - n, 2, n, col); left -= n; }
}

void bootScreen() {
  tft.beginBatch();                 /* окно на ПК не стирает фон под анимацией */
  tft.fillRect(0, 0, SCR_W, SCR_H, C_BG);

  const int bx = 56, by = 76, bw = SCR_W - 112, bh = 96;

  /* ── 1. рамка обводится ── */
  for (int k = 0; k <= 28; k++) {
    bootTrace(bx, by, bw, bh, 28, k, C_ACC);
    delay(18);
  }

  /* ── 2. в середине загорается диод ── */
  int cx = SCR_W / 2, cy = by + bh / 2;
  for (int r = 3; r <= 13; r += 2) {
    tft.drawCircle(cx, cy, r, C_ACC);
    delay(25);
  }
  tft.fillCircle(cx, cy, 5, C_OK);

  /* ── 3. имя ── */
  delay(80);
#if USE_USB_DISPLAY
  tft.bigText(cx, 196, C_WHITE, 22, "ПЛАТА-ОС");          /* центр */
#else
  tft.bigText(26, 196, C_WHITE, 22, "ПЛАТА-ОС");
#endif
  textCenter(240, ("сборка " + String(OS_VER) + " · " + String(OS_CH)).c_str(), F_SM, C_DIM);

  /* ── 4. полоса загрузки ── */
  int w = SCR_W - 96;
  tft.drawRect(48, 268, w, 8, C_LINE);
  for (int p = 0; p <= 100; p += 5) {
    tft.fillRect(50, 270, (w - 4) * p / 100, 4, C_ACC);
    delay(14);
  }

  /* ── 5. три подписи, каждая на своём месте ── */
  const char *steps[3] = { "настройки", "экран", "связь" };
  for (int i = 0; i < 3; i++) {
    tft.fillRect(0, 288, SCR_W, 18, C_BG);
    textCenter(290, steps[i], F_SM, C_DIM);
    delay(120);
  }
  tft.fillRect(0, 288, SCR_W, 18, C_BG);
  textCenter(290, "готово", F_SM, C_OK);
  delay(260);

  tft.endBatch();
}

/* выход: экран гаснет сверху вниз */
void bootOut() {
  for (int y = 0; y < SCR_H; y += 8) {
    tft.fillRect(0, y, SCR_W, 8, C_BG);
    delay(4);
  }
}
