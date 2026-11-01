/* ══════════ ШТОРКА: БЫСТРЫЕ НАСТРОЙКИ ══════════ */
/* Ручка справа в строке состояния есть всегда. Нажал — шторка выехала.
   Внутри то, что нужно каждый день, не заходя в настройки. */


#define SH_Y     22                       /* шторка начинается под строкой */
#define SH_ITEM_H 40

static void shadeItem(int x, int y, int w, const char *title, const char *value,
                      bool on, uint16_t col) {
  box(x, y, w, SH_ITEM_H - 6, on ? C_PANEL2 : C_PANEL, on ? col : C_LINE, 6);
  textDraw(x + 10, y + 4, title, F_SM, on ? C_TXT : C_DIM);
  textDraw(x + 10, y + 19, value, F_SM, on ? col : C_DIM);
}

/* уведомления: собираем из того, что реально происходит */
static int shadeNotes(const char **out, uint16_t *cols, int max) {
  int n = 0;
  static String buf[5];

  if (!wifiOk) {
    buf[n] = wSsid.length() ? "Wi-Fi пропал — ищу сеть" : "Wi-Fi не настроен";
    out[n] = buf[n].c_str(); cols[n] = C_WARN; n++;
  }
  if (updNew && updTag.length()) {
    buf[n] = "Есть прошивка " + updTag;
    out[n] = buf[n].c_str(); cols[n] = C_OK; n++;
  }
  if (pcMac.length() == 0) {
    buf[n] = "Не задан MAC — не разбудить ПК";
    out[n] = buf[n].c_str(); cols[n] = C_DIM; n++;
  }
  if (wifiOk && rssiVal < -78) {
    buf[n] = "Слабый сигнал: " + String(rssiVal) + " dBm";
    out[n] = buf[n].c_str(); cols[n] = C_WARN; n++;
  }
  if (n == 0) {
    buf[0] = "Всё в порядке";
    out[0] = buf[0].c_str(); cols[0] = C_OK; n = 1;
  }
  return n > max ? max : n;
}

/* раскладка — считаем один раз, чтобы касания точно совпадали с картинкой */
struct ShadeBox { int y, h; const char *name; };

int shClose = SH_Y, shBright = 0, shVol = 0, shNotes = 0, shBottom = SCR_H - 34;

void drawShade() {
  /* фон — обычный экран, сверху шторка */
  if (curScreen && curScreen->draw && curScreen != &SCR_SHADE) {
    curScreen->draw();
  } else {
    drawLauncher();
  }

  int y = SH_Y;
  tft.fillRect(0, y, SCR_W, SCR_H - y, C_BG);
  tft.fillRect(0, y, SCR_W, 20, C_PANEL);
  textDraw(8, y + 4, "БЫСТРО", F_SM, C_ACC);
  textRight(SCR_W - 12, y + 4, "СВЕРНУТЬ ▲", F_SM, C_TXT);
  shClose = y; y += 26;

  /* ── яркость ── */
  box(8, y, SCR_W - 16, 44, C_PANEL, C_LINE, 6);
  textDraw(16, y + 4, "ЯРКОСТЬ ЭКРАНА", F_SM, C_DIM);
  textRight(SCR_W - 16, y + 4, String(blPwm).c_str(), F_SM, C_TXT);
  barDraw(16, y + 24, SCR_W - 32, 14, (int)blPwm * 100 / 255, C_WARN);
  shBright = y + 22; y += 50;

  /* ── громкость ПК ── */
  box(8, y, SCR_W - 16, 44, C_PANEL, C_LINE, 6);
  textDraw(16, y + 4, "ГРОМКОСТЬ ПК", F_SM, C_DIM);
  textRight(SCR_W - 16, y + 4, pcMuted ? "выкл" : (String(pcVol) + "%").c_str(),
            F_SM, pcMuted ? C_ERR : C_TXT);
  barDraw(16, y + 24, SCR_W - 32, 14, pcVol, pcMuted ? C_ERR : C_ACC);
  shVol = y + 22; y += 50;

  /* ── уведомления ── */
  textDraw(10, y, "УВЕДОМЛЕНИЯ", F_SM, C_DIM);
  shNotes = y + 18; y += 18;
  const char *notes[5];
  uint16_t nc[5];
  int nn = shadeNotes(notes, nc, 3);
  for (int i = 0; i < nn; i++) {
    int yy = y + i * 32;
    box(8, yy, SCR_W - 16, 28, C_PANEL, C_LINE, 6);
    tft.fillRoundRect(8, yy, 4, 28, 2, nc[i]);        /* цветная метка слева */
    textDraw(18, yy + 7, notes[i], F_SM, nc[i]);
  }
  y += nn * 32;

  /* ── что на связи ── */
  int ny = y > SCR_H - 86 ? y : SCR_H - 86;
  box(8, ny, SCR_W - 16, 38, C_PANEL, C_LINE, 6);
  textDraw(16, ny + 6, wifiOk ? wSsid.c_str() : "нет Wi-Fi", F_SM,
           wifiOk ? C_TXT : C_ERR);
  textDraw(16, ny + 21, ("плата " + (boardIp.length() ? boardIp : String("—"))).c_str(),
           F_SM, C_DIM);

  btnDraw(8, SCR_H - 34, SCR_W - 16, 30, "ЗАКРЫТЬ ▲", C_PANEL2, C_TXT, F_MD);
  shBottom = SCR_H - 34;
}

void openShade() { shadeOpen = true; go(&SCR_SHADE); }
void closeShade() { shadeOpen = false; go(&SCR_LAUNCH); }

void shadeTouch(TouchPt t) {
  if (t.y >= shBottom) { closeShade(); return; }         /* кнопка внизу   */
  if (t.y <= shClose + 20) { closeShade(); return; }     /* СВЕРНУТЬ ▲    */
  if (t.x >= SCR_W - 34 && t.y <= STATUS_H) { closeShade(); return; }  /* ручка */

  /* яркость экрана */
  if (t.y >= shBright && t.y < shBright + 20) {
    blSet(constrain(map(t.x, 16, SCR_W - 16, 10, 255), 10, 255));
    cfgSave();
    drawShade();
    return;
  }

  /* громкость ПК */
  if (t.y >= shVol && t.y < shVol + 20) {
    pcCmd("vol_up");
    drawShade();
    return;
  }

  /* тап по уведомлению — открыть обновление или настройки */
  if (t.y >= shNotes && t.y < shNotes + 96) {
    if (updNew) { shadeOpen = false; go(&SCR_UPD); }
    else { shadeOpen = false; go(&SCR_SET); }
    return;
  }
}
