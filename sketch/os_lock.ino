/* ══════════ ЭКРАН БЛОКИРОВКИ ══════════ */
/* Как на телефоне: большие часы, дата, строка уведомлений.
   Будильник D4 или касание — разблокировать. */

static bool locked = false;

void drawLock() {
  tft.fillRect(0, 0, SCR_W, SCR_H, C_BG);

  /* верхняя полоска с сетью */
  tft.fillRect(0, 0, SCR_W, 3, C_ACC);

  struct tm t;
  bool ok = nowTime(t);
  char hhmm[8] = "--:--", dt[32] = "";
  if (ok) {
    strftime(hhmm, sizeof(hhmm), "%H:%M", &t);
    static const char *mon[12] = { "янв", "фев", "мар", "апр", "мая", "июн",
                                   "июл", "авг", "сен", "окт", "ноя", "дек" };
    char d2[24];
    strftime(d2, sizeof(d2), "%d", &t);
    int m = t.tm_mon; if (m < 0 || m > 11) m = 0;
    dt[0] = 0;
    snprintf(dt, sizeof(dt), "%s %s, %s", d2, mon[m],
             t.tm_wday == 0 ? "вс" : t.tm_wday == 1 ? "пн" : t.tm_wday == 2 ? "вт" :
             t.tm_wday == 3 ? "ср" : t.tm_wday == 4 ? "чт" : t.tm_wday == 5 ? "пт" : "сб");
  }

  /* часы ровно по центру — своим шрифтом, ширина считается сама */
  textCenter(78, hhmm, F_LG, C_WHITE);
  textCenter(146, dt, F_MD, C_DIM);

  /* погода рядом с датой */
  if (weatherT != "—") {
    box(20, 170, SCR_W - 40, 26, C_PANEL, C_LINE, 8);
    textDraw(30, 176, weatherT.c_str(), F_MD, C_ACC);
    if (weatherC.length())
      textDraw(30 + textW(weatherT.c_str(), F_MD) + 10, 178,
               textClip(weatherC, 120, F_SM).c_str(), F_SM, C_DIM);
  }

  /* строка уведомлений */
  const char *note = "всё спокойно";
  uint16_t ncol = C_DIM;
  if (!wifiOk) { note = "нет сети"; ncol = C_WARN; }
  else if (updNew) { note = "есть обновление"; ncol = C_OK; }
  else if (pcMuted) { note = "звук на ПК выключен"; ncol = C_DIM; }

  box(16, 204, SCR_W - 32, 30, C_PANEL, C_LINE, 8);
  tft.fillCircle(30, 219, 4, ncol);
  textDraw(42, 212, note, F_SM, ncol);

  /* подсказка снизу */
  textCenter(SCR_H - 54, "коснись экрана", F_MD, C_TXT);
  textCenter(SCR_H - 32, "или зажми D4", F_SM, C_DIM);

  /* аватарка пользователя */
  if (uAva() || uName().length()) avaDraw(SCR_W / 2, 262, 20, uAva());
  if (uName().length()) textCenter(292, uName().c_str(), F_SM, C_DIM);
}

void lockTouch(TouchPt t) {
  (void)t;
  locked = false;
  go(&SCR_LAUNCH);
  logAdd("разблокировано");
}

/* перейти в блокировку */
void lockNow() {
  locked = true;
  go(&SCR_LOCK);
}
