/* ══════════ ТАЙМЕР ══════════ */
/* Обратный отсчёт: 1, 3, 5, 10 минут. По нулю — диод светится. */

uint32_t tmEnd = 0;         /* когда закончится, millis() */
uint16_t tmSet = 0;         /* на сколько поставили, секунд */
bool     tmRun = false;

void drawTimer() {
  topBack("ТАЙМЕР", 18);

  uint32_t left = 0;
  if (tmRun) {
    uint32_t now = millis();
    left = (now >= tmEnd) ? 0 : (tmEnd - now) / 1000;
    if (left == 0) {
      tmRun = false;
      ledLit = true;
      ledApply();
    }
  } else if (tmSet) {
    left = tmSet;
  }

  char buf[16];
  snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)(left / 60), (unsigned)(left % 60));
  headline(0, 92, buf, tmRun ? C_ACC : C_WHITE, 42);
  textCenter(146, tmRun ? "идёт отсчёт" : (tmSet ? "готов" : "выбери время"),
             F_SM, C_DIM);

  const char *op[4] = { "1 мин", "3 мин", "5 мин", "10 мин" };
  const uint16_t sec[4] = { 60, 180, 300, 600 };
  for (int i = 0; i < 4; i++) {
    int x = 10 + (i % 2) * 114, y = 174 + (i / 2) * 40;
    btnDraw(x, y, 108, 34, op[i], tmSet == sec[i] ? C_ACC : C_PANEL,
            tmSet == sec[i] ? C_WHITE : C_TXT, F_SM);
  }

  btnDraw(10, 258, 108, 34, "СТОП", C_PANEL, C_TXT, F_SM);
  btnDraw(124, 258, 106, 34, "ПУСК", C_OK, C_WHITE, F_SM);
}

void timerTouch(TouchPt t) {
  if (topBackHit(t)) { tmRun = false; go(&SCR_LAUNCH); return; }

  if (t.y >= 174 && t.y < 252) {
    const uint16_t sec[4] = { 60, 180, 300, 600 };
    int i = ((t.x - 10) / 114) + ((t.y - 174) / 40) * 2;
    if (i < 0 || i > 3) return;
    tmSet = sec[i];
    tmRun = false;
    go(&SCR_TIMER);
    return;
  }
  if (t.y >= 258 && t.y < 292) {
    if (t.x < 118) { tmRun = false; go(&SCR_TIMER); return; }
    if (!tmSet) return;
    tmEnd = millis() + (uint32_t)tmSet * 1000UL;
    tmRun = true;
    ledLit = false;
    neopixelWrite(RGB_PIN, 0, 0, 0);
    go(&SCR_TIMER);
    return;
  }
}

/* ══════════ СЕКУНДОМЕР ══════════ */
uint32_t swBase = 0, swAcc = 0;
bool     swRun = false;

void drawStop() {
  topBack("СЕКУНДОМЕР", 16);

  uint32_t ms = swAcc + (swRun ? millis() - swBase : 0);
  uint32_t cs = ms / 10 % 100;
  uint32_t s = ms / 1000 % 60;
  uint32_t m = ms / 60000;

  char buf[20];
  snprintf(buf, sizeof(buf), "%02u:%02u.%02u", (unsigned)m, (unsigned)s, (unsigned)cs);
  headline(0, 100, buf, swRun ? C_OK : C_WHITE, 34);
  textCenter(150, swRun ? "идёт" : "стоит", F_SM, C_DIM);

  btnDraw(10, 194, 220, 40, swRun ? "СТОП" : "ПУСК",
          swRun ? C_ERR : C_OK, C_WHITE, F_MD);
  btnDraw(10, 244, 220, 40, "СБРОС", C_PANEL, C_TXT, F_MD);
  textCenter(SCR_H - 20, "кружок: сохраняется сам", F_SM, C_DIM);
}

void stopTouch(TouchPt t) {
  if (topBackHit(t)) { go(&SCR_MORE); return; }
  if (t.y >= 194 && t.y < 234) {
    if (swRun) { swAcc += millis() - swBase; swRun = false; }
    else       { swBase = millis(); swRun = true; }
    go(&SCR_STOP);
    return;
  }
  if (t.y >= 244 && t.y < 284) { swAcc = 0; swRun = false; go(&SCR_STOP); return; }
}

/* ══════════ ЗАМЕТКА ══════════ */
/* Короткая записка на экране: список, молоко, что угодно. */

String noteText = "";

void drawNote() {
  topBack("ЗАМЕТКА", 18);

  box(10, 52, SCR_W - 20, 150, C_PANEL, C_LINE, 6);

  /* текст с переносом по словам */
  String rest = noteText.length() ? noteText : "пусто — нажми и напиши";
  int y = 60, lim = SCR_W - 34;
  while (rest.length() && y < 190) {
    int cut = rest.length();
    /* ищем, сколько влезает */
    while (cut > 1 && textW(rest.substring(0, cut).c_str(), F_SM) > lim) cut--;
    if (cut < rest.length()) {                 /* отступить до пробела */
      int sp = rest.lastIndexOf(' ', cut);
      if (sp > 0) cut = sp;
    }
    textDraw(18, y, rest.substring(0, cut).c_str(), F_SM,
             noteText.length() ? C_TXT : C_DIM);
    rest = rest.substring(cut);
    rest.trim();
    y += 18;
  }

  btnDraw(10, 212, SCR_W - 20, 34, "НАПИСАТЬ", C_ACC, C_WHITE, F_MD);
  btnDraw(10, 254, 108, 32, "СТЕРЕТЬ", C_PANEL, C_TXT, F_SM);
  btnDraw(124, 254, 106, 32, "ГОТОВО", C_PANEL, C_TXT, F_SM);
}

void noteTouch(TouchPt t) {
  if (topBackHit(t)) { go(&SCR_MORE); return; }
  if (t.y >= 212 && t.y < 246) {
    String buf = noteText;
    if (kbInput("ЗАМЕТКА", buf)) noteText = buf;
    go(&SCR_NOTE);
    return;
  }
  if (t.y >= 254 && t.y < 286) {
    if (t.x < 118) { noteText = ""; go(&SCR_NOTE); return; }
    go(&SCR_MORE);
    return;
  }
}
