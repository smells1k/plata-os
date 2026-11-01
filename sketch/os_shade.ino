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

void drawShade() {
  /* фон — обычный экран, сверху шторка */
  if (curScreen && curScreen->draw && curScreen != &SCR_SHADE) {
    curScreen->draw();
  } else {
    drawLauncher();
  }

  int y = SH_Y;
  tft.fillRect(0, y, SCR_W, SCR_H - y, C_BG);
  tft.fillRect(0, y, SCR_W, 18, C_PANEL);
  textDraw(8, y + 3, "БЫСТРО", F_SM, C_ACC);
  textRight(SCR_W - 38, y + 3, "закрыть ▼", F_SM, C_DIM);
  y += 22;

  /* ── яркость ── */
  box(8, y, SCR_W - 16, 42, C_PANEL, C_LINE, 6);
  textDraw(16, y + 4, "ЯРКОСТЬ ЭКРАНА", F_SM, C_DIM);
  textRight(SCR_W - 16, y + 4, String(blPwm).c_str(), F_SM, C_TXT);
  barDraw(16, y + 22, SCR_W - 32, 12, (int)blPwm * 100 / 255, C_WARN);
  shadeY[0] = y; y += 48;

  /* ── громкость ПК ── */
  box(8, y, SCR_W - 16, 42, C_PANEL, C_LINE, 6);
  textDraw(16, y + 4, "ГРОМКОСТЬ ПК", F_SM, C_DIM);
  textRight(SCR_W - 16, y + 4, pcMuted ? "выкл" : (String(pcVol) + "%").c_str(),
            F_SM, pcMuted ? C_ERR : C_TXT);
  barDraw(16, y + 22, SCR_W - 32, 12, pcVol, pcMuted ? C_ERR : C_ACC);
  shadeY[1] = y; y += 48;

  /* ── плитки 3×2 ── */
  const char *names[6] = { "Свет", "Часы", "Игры", "Настр.", "Обновл.", "ПК" };
  for (int i = 0; i < 6; i++) {
    int c = i % 3, r = i / 3;
    int x = 8 + c * 78, yy = y + r * 46;
    bool on = (i == 0 && ledR + ledG + ledB > 0) || (i == 5 && pcHost.length());
    shadeItem(x, yy, 74, names[i],
              i == 0 ? (String("ярк ") + ledBri).c_str()
                     : (i == 5 ? (pcHost.length() ? "на связи" : "нет связи")
                               : (i == 1 ? (wifiOk ? "сеть есть" : "нет сети")
                                         : (i == 4 ? (updNew ? "есть новая" : "свежая")
                                                   : "открыть"))),
              on, i == 5 ? C_OK : C_ACC);
  }
  y += 92;

  /* ── уведомления ── */
  textDraw(8, y, "УВЕДОМЛЕНИЯ", F_SM, C_DIM);
  y += 16;
  const char *notes[4];
  uint16_t ncol[4];
  int n = shadeNotes(notes, ncol, 4);
  for (int i = 0; i < n && y < SCR_H - 40; i++) {
    box(8, y, SCR_W - 16, 26, C_PANEL, C_LINE, 5);
    tft.fillCircle(18, y + 13, 3, ncol[i]);
    textDraw(28, y + 5, textClip(notes[i], SCR_W - 46, F_SM).c_str(), F_SM, C_TXT);
    y += 30;
  }

  btnDraw(8, SCR_H - 32, SCR_W - 16, 26, "ЗАКРЫТЬ ▼", C_PANEL2, C_TXT, F_SM);
}

void openShade() { shadeOpen = true; go(&SCR_SHADE); }
void closeShade() { shadeOpen = false; go(&SCR_LAUNCH); }

void shadeTouch(TouchPt t) {
  /* закрыть */
  if (t.y >= SCR_H - 34 || (t.y <= SH_Y + 20 && t.x > SCR_W - 90)) { closeShade(); return; }
  if (t.y <= SH_Y) { closeShade(); return; }        /* ручка = закрыть */

  /* яркость */
  if (t.y >= shadeY[0] && t.y < shadeY[0] + 42) {
    blSet(constrain(map(t.x, 16, SCR_W - 16, 10, 255), 10, 255));
    cfgSave();
    drawShade();
    return;
  }
  /* громкость ПК */
  if (t.y >= shadeY[1] && t.y < shadeY[1] + 42) {
    pcCmd("vol_up");
    drawShade();
    return;
  }

  /* плитки */
  int ty = shadeY[1] + 48;
  if (t.y >= ty && t.y < ty + 92) {
    int c = (t.x - 8) / 78, r = (t.y - ty) / 46;
    if (c < 0 || c > 2 || r < 0 || r > 1) return;
    switch (r * 3 + c) {
      case 0: go(&SCR_LIGHT); break;
      case 1: go(&SCR_CLOCK); break;
      case 2: go(&SCR_GAMES); break;
      case 3: go(&SCR_SET); break;
      case 4: go(&SCR_UPD); break;
      case 5: go(&SCR_PC); break;
    }
    shadeOpen = false;
    return;
  }
}
