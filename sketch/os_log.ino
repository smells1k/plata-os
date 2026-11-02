/* ══════════ ЖУРНАЛ: что было с платой ══════════ */
/* Пишем только важное: включилась, обновилась, пропала сеть, сменился цвет.
   Живёт в энергонезависимой памяти, переживает перезагрузку и обновление. */

#define LOG_MAX 30
static String  logLines[LOG_MAX];
static int     logCount = 0;

/* добавить запись: сверху дата и время, дальше текст */
void logAdd(const char *what) {
  struct tm t;
  char stamp[16] = "--:--";
  if (nowTime(t)) strftime(stamp, sizeof(stamp), "%d.%m %H:%M", &t);

  /* сдвигаем вниз, новая сверху */
  for (int i = LOG_MAX - 1; i > 0; i--) logLines[i] = logLines[i - 1];
  logLines[0] = String(stamp) + "  " + what;
  if (logCount < LOG_MAX) logCount++;
  logSave();
}

void logSave() {
  cfg.begin("log", false);
  cfg.putUChar("n", (uint8_t)logCount);
  for (int i = 0; i < logCount; i++) cfg.putString(("l" + String(i)).c_str(), logLines[i]);
  cfg.end();
}

void logLoad() {
  cfg.begin("log", true);
  logCount = cfg.getUChar("n", 0);
  if (logCount > LOG_MAX) logCount = LOG_MAX;
  for (int i = 0; i < logCount; i++)
    logLines[i] = cfg.getString(("l" + String(i)).c_str(), "");
  cfg.end();
}

/* ── экран журнала ── */
static int logScroll = 0;
#define LOG_ROWS 8

void drawLog() {
  topBack("ЖУРНАЛ", 16);

  if (logCount == 0) {
    textCenter(120, "пока ничего не было", F_MD, C_DIM);
    textCenter(148, "тут появятся важные события", F_SM, C_DIM);
    return;
  }

  int shown = logCount - logScroll;
  if (shown > LOG_ROWS) shown = LOG_ROWS;

  for (int i = 0; i < shown; i++) {
    int y = 52 + i * 30;
    const String &s = logLines[i + logScroll];
    box(8, y, SCR_W - 16, 26, C_PANEL, C_LINE, 5);
    /* время — отдельным цветом, дальше текст */
    int sp = s.indexOf("  ");
    if (sp > 0) {
      textDraw(14, y + 6, s.substring(0, sp).c_str(), F_SM, C_ACC);
      textDraw(90, y + 6, textClip(s.substring(sp + 2), SCR_W - 104, F_SM).c_str(), F_SM, C_TXT);
    } else {
      textDraw(14, y + 6, textClip(s, SCR_W - 28, F_SM).c_str(), F_SM, C_TXT);
    }
  }

  /* прокрутка */
  if (logCount > LOG_ROWS) {
    textCenter(SCR_H - 34, ("▲ " + String(logScroll + 1) + "–" +
               String(logScroll + shown) + " из " + String(logCount)).c_str(), F_SM, C_DIM);
    btnDraw(8, SCR_H - 26, SCR_W - 16, 22,
            logScroll + LOG_ROWS >= logCount ? "ВВЕРХ" : "ВНИЗ ▾", C_PANEL2, C_TXT, F_SM);
  }
}

void logTouch(TouchPt t) {
  if (topBackHit(t)) { go(&SCR_MORE); return; }
  if (logCount > LOG_ROWS && t.y >= SCR_H - 30) {
    logScroll += LOG_ROWS;
    if (logScroll >= logCount) logScroll = 0;
    go(&SCR_LOG);
  }
}
