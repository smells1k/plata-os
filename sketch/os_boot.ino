/* ══════════ ЭКРАН ЗАПУСКА ══════════ */
/* Короткая заставка: логотип собирается по частям, строка загрузки,
   название. Диод при старте не зажигаем. */

static void bootDot(int x, int y, int r, uint16_t col) {
  tft.fillCircle(x, y, r, col);
}

/* спираль из точек — «включается» */
static void bootSpiral(int step, uint16_t col) {
  for (int i = 0; i < step && i < 36; i++) {
    float a = i * 0.55f;
    int r = 4 + i * 2;
    int x = SCR_W / 2 + (int)(cos(a) * r);
    int y = 116 + (int)(sin(a) * r);
    bootDot(x, y, (i < 18) ? 3 : 2, col);
  }
}

void bootScreen() {
  tft.fillScreen(C_BG);

  /* ── 1. спираль ── */
  for (int k = 6; k <= 36; k += 6) {
    bootSpiral(k, C_ACC);
    delay(60);
  }

  /* ── 2. рамка логотипа ── */
  for (int i = 0; i < 8; i++) {
    int w = 40 + i * 22;
    int h = 40 + i * 14;
    tft.drawRoundRect(SCR_W / 2 - w / 2, 116 - h / 2, w, h, 10, C_ACC);
    delay(35);
  }

  /* ── 3. имя ── */
  tft.fillRoundRect(30, 176, SCR_W - 60, 3, 1, C_ACC);
#if USE_USB_DISPLAY
  tft.bigText(SCR_W / 2, 190, C_WHITE, 22, "ПЛАТА-ОС");      /* по центру */
#else
  tft.bigText(20, 190, C_WHITE, 22, "ПЛАТА-ОС");
#endif
  textCenter(228, ("сборка " + String(OS_VER) + " · " + String(OS_CH)).c_str(),
             F_SM, C_DIM);

  /* ── 4. полоса загрузки ── */
  int w = SCR_W - 80;
  tft.drawRect(40, 258, w, 10, C_LINE);
  for (int p = 0; p <= 100; p += 4) {
    tft.fillRect(42, 260, (w - 4) * p / 100, 6, C_ACC);
    if (p % 28 == 0) delay(40);
  }

  /* ── 5. что грузим ── */
  const char *steps[3] = { "настройки", "экран", "связь" };
  for (int i = 0; i < 3; i++) {
    textCenter(284, steps[i], F_SM, C_DIM);
    delay(140);
  }
  textCenter(284, "готово", F_SM, C_OK);
  delay(220);
}

/* заставка при выходе: экран гаснет волной */
void bootOut() {
  for (int y = 0; y < SCR_H; y += 8) {
    tft.fillRect(0, y, SCR_W, 8, C_BG);
    delay(4);
  }
}
