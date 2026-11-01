/*
 *  os_extra.ino — служебные экраны ПЛАТА-ОС:
 *  настройки, дополнительно, фонарик, ребут, система
 *
 *  Списки «Дополнительно» и «Система» не помещаются на экран 240x320,
 *  поэтому рисуем с прокруткой: плата отдаёт команду scrollH(h),
 *  окно на ПК позволяет крутить колесом.
 */

/* ══════════ НАСТРОЙКИ ══════════ */
int g_setIpY = 0, g_setMacY = 0, g_setBlY = 0, g_setCalY = 0, g_setScanY = 0, g_setMoreY = 0;
int g_setPortY = 0, g_setPcY = 0, g_setBackY = 0;

void drawSet() {
  topBack("НАСТРОЙКИ", 18);

  /* ── Wi-Fi: часы, погода, обновление по воздуху ── */
  box(10, 46, SCR_W - 20, 52, C_PANEL, C_LINE, 6);
  tft.fillCircle(SCR_W - 20, 58, 4, wifiOk ? C_OK : C_ERR);
  textDraw(18, 50, "WI-FI", F_SM, C_DIM);
  String s = wifiOk ? wSsid + " · " + boardIp
                    : (wSsid.length() ? wSsid + " · нет связи" : "не настроен");
  textDraw(18, 64, textClip(s, SCR_W - 58, F_SM).c_str(), F_SM, wifiOk ? C_OK : C_WARN);
  textDraw(18, 79, wifiOk ? "обновление: plata-os.local" : "обновление: нужен Wi-Fi",
           F_SM, C_DIM);

  int y = 102;
  btnDraw(10, y, SCR_W - 20, 28, wifiOk ? "Сменить сеть" : "Найти сети",
          C_ACC, C_WHITE, F_SM);
  g_setScanY = y; y += 32;

  box(10, y, SCR_W - 20, 36, C_PANEL, C_LINE, 6);
  textDraw(18, y + 2, "ЭКРАН", F_SM, C_DIM);
  barDraw(18, y + 19, SCR_W - 46, 11, (int)blPwm * 100 / 255, C_WARN);
  textRight(SCR_W - 24, y + 2, String(blPwm).c_str(), F_SM, C_TXT);
  g_setBlY = y; y += 40;

  btnDraw(10, y, SCR_W - 20, 28, "Связь с ПК (кабель)", C_PANEL, C_TXT, F_SM);
  g_setPcY = y; y += 32;

  box(10, y, SCR_W - 20, 36, C_PANEL, C_LINE, 6);
  textDraw(18, y + 2, "MAC ДЛЯ РАЗБУДКИ", F_SM, C_DIM);
  textDraw(18, y + 17, textClip(pcMac.length() ? pcMac : "не задан нажми", SCR_W - 40, F_SM).c_str(),
           F_SM, pcMac.length() ? C_TXT : C_WARN);
  g_setMacY = y; y += 40;

  btnDraw(10, y, SCR_W - 20, 28, "Дополнительно  ›", C_PANEL, C_TXT, F_SM);
  g_setMoreY = y; y += 32;

  g_setBackY = y + 30;      /* нижней кнопки больше нет — назад сверху */

  g_setIpY = g_setPortY = -100;     /* по кабелю не нужны */
}

void setTouch(TouchPt t) {
  if (topBackHit(t)) { go(&SCR_LAUNCH); return; }
  if (t.y >= g_setMoreY && t.y < g_setMoreY + 28) { go(&SCR_MORE); return; }
  if (t.y >= g_setScanY && t.y < g_setScanY + 28) { wifiDialog(); return; }
  if (t.y >= g_setBlY && t.y < g_setBlY + 36) {
    blSet(constrain(map(t.x, 10, SCR_W - 10, 10, 255), 10, 255));
    cfgSave(); go(&SCR_SET); return;
  }
  if (t.y >= g_setIpY && t.y < g_setIpY + 38) {
    String buf = pcIp;
    if (kbInput("IP КОМПЬЮТЕРА", buf, true)) { pcIp = buf; cfgSave(); }
    go(&SCR_SET); return;
  }
  if (t.y >= g_setPortY && t.y < g_setPortY + 38) {
    String buf = String(pcPort);
    if (kbInput("ПОРТ АГЕНТА (1-65535)", buf, true)) {
      long v = buf.toInt();
      if (v >= 1 && v <= 65535) { pcPort = (uint16_t)v; cfgSave(); }
    }
    go(&SCR_SET); return;
  }
  if (t.y >= g_setPcY && t.y < g_setPcY + 28) { pcDebug(); go(&SCR_PC); return; }
  if (t.y >= g_setMacY && t.y < g_setMacY + 36) {
    String buf = pcMac;
    if (kbInput("MAC (AA:BB:CC:DD:EE:FF)", buf)) { pcMac = buf; cfgSave(); }
    go(&SCR_SET); return;
  }

}

/* ══════════ ДОПОЛНИТЕЛЬНО (список с прокруткой) ══════════ */
#define MORE_ITEMS 9
#define MORE_TOP   52
#define MORE_STEP  30
#define MORE_H     (SCR_H)   /* всё влезает, прокрутка не нужна */  /* + кнопка НАЗАД */

void drawMore() {
  topBack("ДОПОЛНИТЕЛЬНО", 14);

  const char *items[MORE_ITEMS] = {
    "Цвет диода",
    "Таймер",
    "Секундомер",
    "Обновление по Wi-Fi",
    "Калибровка сенсора",
    "Перезагрузить",
    "Система",
    "Обновить время",
    "Заметка",
  };
  uint16_t acc[MORE_ITEMS] = { 0xE1F0, 0x2D7F, 0x36E6, 0x2D7F, 0x6B4D,
                               0xE8A4, 0x8010, 0x3666, 0xFBE0 };

  for (int i = 0; i < MORE_ITEMS; i++) {
    int y = MORE_TOP + i * MORE_STEP;
    box(10, y, SCR_W - 20, 26, C_PANEL, C_LINE, 6);
    tft.fillCircle(22, y + 13, 3, acc[i]);
    textDraw(32, y + 6, items[i], F_SM, C_TXT);
    textRight(SCR_W - 18, y + 6, "›", F_SM, C_DIM);
  }
}

void moreTouch(TouchPt t) {
  if (topBackHit(t)) { go(&SCR_LAUNCH); return; }

  int i = (t.y - MORE_TOP) / MORE_STEP;
  if (i < 0 || i >= MORE_ITEMS) return;
  switch (i) {
    case 0: go(&SCR_COLOR); break;
    case 1: go(&SCR_TIMER); break;
    case 2: go(&SCR_STOP);  break;
    case 3: go(&SCR_UPD);   break;
    case 4: calibrate(); go(&SCR_SET); break;
    case 5: go(&SCR_REBOOT); break;
    case 6: go(&SCR_SYS);    break;
    case 7: {
      String nt;
      if (wifiOk) {
        configTime(0, 0, "pool.ntp.org", "time.google.com");
        struct tm tt;
        nt = getLocalTime(&tt, 0) ? "время обновлено" : "не вышло";
      } else {
        nt = "нужен Wi-Fi";
      }
      drawMore();
      textCenter(SCR_H - 24, nt.c_str(), F_SM, C_OK);
      delay(1400);
      go(&SCR_MORE);
    } break;
    case 8: go(&SCR_NOTE); break;
    default: go(&SCR_LAUNCH); break;
  }
}

/* ══════════ ФОНАРИК ══════════ */
bool torchOn = false;

void drawTorch() {
  topBack("ФОНАРИК", 20);

  box(10, 56, SCR_W - 20, 140, torchOn ? 0xFFFF : C_PANEL, C_LINE, 12);
  headline(30, 108, torchOn ? "ВКЛ" : "ВЫКЛ", torchOn ? C_ACC : C_DIM, 32);
  textDraw(10, 210, "диод D8 светит белым", F_SM, C_DIM);

  btnDraw(10, SCR_H - 96, SCR_W - 20, 40, torchOn ? "ВЫКЛЮЧИТЬ" : "ВКЛЮЧИТЬ",
          torchOn ? C_ERR : C_ACC, C_WHITE, F_MD);
}

void torchTouch(TouchPt t) {
  if (t.y >= SCR_H - 48) {
    torchOn = false;
    ledApply();
    go(&SCR_MORE);
    return;
  }
  if (t.y >= SCR_H - 96) {
    torchOn = !torchOn;
    if (torchOn) {
      ledMode = 0; ledR = ledG = ledB = 255; ledBri = 255; ledLit = true;
      showColor(255, 255, 255);
    } else {
      ledApply();
    }
    go(&SCR_TORCH);
  }
}

/* ══════════ ПРОВЕРКА ДИОДА ══════════ */
void ledTest() {
  tft.fillScreen(C_BG);
  topBack("ТЕСТ ДИОДА", 20);
  textDraw(14, 60, "смотри на диод D8", F_SM, C_DIM);
  uint8_t oldR = ledR, oldG = ledG, oldB = ledB;
  int oldMode = ledMode;
  ledMode = 0; ledBri = 255; ledLit = true;

  struct Step { const char *n; uint8_t r, g, b; } steps[6] = {
    { "красный", 255, 0, 0 }, { "зелёный", 0, 255, 0 }, { "синий", 0, 0, 255 },
    { "белый", 255, 255, 255 }, { "жёлтый", 255, 200, 0 }, { "фиолетовый", 200, 0, 255 },
  };
  for (int i = 0; i < 6; i++) {
    neopixelWrite(RGB_PIN, steps[i].r, steps[i].g, steps[i].b);
    tft.fillRect(14, 100, SCR_W - 28, 60, C_PANEL);
    headline(24, 118, steps[i].n, C_TXT, 20);
    delay(700);
  }
  neopixelWrite(RGB_PIN, 0, 0, 0);
  ledR = oldR; ledG = oldG; ledB = oldB; ledMode = oldMode;
  ledApply();
  tft.fillScreen(C_BG);
  topBack("готово", 20);
  delay(700);
  go(&SCR_MORE);
}

/* ══════════ СИСТЕМА (список с прокруткой) ══════════ */
#define SYS_ROWS 13
#define SYS_TOP  54
#define SYS_STEP 26
#define SYS_H    (SYS_TOP + SYS_ROWS * SYS_STEP + 48)   /* последняя кнопка + НАЗАД */

void drawSys() {
  topBack("СИСТЕМА", 20);
  tft.scrollH(SYS_H);

  char up[32];
  uint32_t sec = millis() / 1000;
  snprintf(up, sizeof(up), "%lu:%02lu:%02lu", sec / 3600, (sec / 60) % 60, sec % 60);

  struct Row { const char *k; String v; };
  Row rows[SYS_ROWS] = {
    { "Плата",        "FireBeetle 2 ESP32-E" },
    { "Чип",          ESP.getChipModel() },
    { "Ядра",         String(ESP.getChipCores()) + " × " + getCpuFrequencyMhz() + " МГц" },
    { "Память",       String(ESP.getFreeHeap() / 1024) + " КБ" },
    { "Минимум",      String(ESP.getMinFreeHeap() / 1024) + " КБ" },
    { "Флеш",         String(ESP.getFlashChipSize() / 1048576) + " МБ" },
    { "Температура",  String(temperatureRead(), 1) + " °C" },
    { "Работаю",      up },
    { "Версия ОС",    OS_VER },
    { "Сеть",         wifiOk ? wSsid : "нет" },
    { "Адрес",        wifiOk ? boardIp : "—" },
    { "Сигнал",       wifiOk ? String(rssiVal) + " dBm" : "—" },
    { "Диод D8",      ledLit ? String(ledR) + "," + ledG + "," + ledB : "выключен" },
  };

  for (int i = 0; i < SYS_ROWS; i++) {
    int y = SYS_TOP + i * SYS_STEP;
    if (i % 2 == 0) tft.fillRect(8, y - 2, SCR_W - 16, SYS_STEP - 2, C_PANEL);
    textDraw(14, y + 4, rows[i].k, F_SM, C_DIM);
    textRight(SCR_W - 14, y + 4, textClip(rows[i].v, 118, F_SM).c_str(), F_SM, C_TXT);
  }

}

void sysTouch(TouchPt t) {
  if (topBackHit(t)) { go(&SCR_MORE); return; }
}

/* ══════════ ПЕРЕЗАГРУЗКА ══════════ */
void drawReboot() {
  topBack("ПЕРЕЗАГРУЗКА", 20);
  box(14, 70, SCR_W - 28, 100, C_PANEL, C_LINE, 10);
  textDraw(26, 90, "Плата выключится", F_SM, C_TXT);
  textDraw(26, 110, "и включится заново.", F_SM, C_TXT);
  textDraw(26, 138, "Настройки сохранятся.", F_SM, C_DIM);

  btnDraw(10, 190, SCR_W - 20, 42, "ДА, ПЕРЕЗАГРУЗИТЬ", C_ERR, C_WHITE, F_SM);
  btnDraw(10, 240, SCR_W - 20, 34, "ОТМЕНА", C_PANEL, C_TXT, F_SM);
}

void rebootTouch(TouchPt t) {
  if (t.y >= 190 && t.y <= 232) {
    neopixelWrite(RGB_PIN, 0, 0, 0);
    tft.fillScreen(C_BG);
    textCenter(150, "перезагружаюсь…", F_MD, C_TXT);
    delay(600);
    ESP.restart();
    return;
  }
  go(&SCR_MORE);
}

/* ══════════ ИГРЫ: меню с прокруткой ══════════ */
#define GM_ITEMS 7
#define GM_TOP   54
#define GM_STEP  36
#define GM_H     (SCR_H)     /* всё влезает, прокрутка не нужна */

void drawGames() {
  topBack("ИГРЫ", 20);
  tft.scrollH(GM_H);

  const char *items[GM_ITEMS] = {
    "Проверь память", "Реакция", "Саймон",
    "Крестики-нолики", "Кубик", "Монетка",
    "Угадай цвет"
  };
  uint16_t acc[GM_ITEMS] = { 0x2D7F, 0x3666, 0xFBE0, 0x8010, 0xC986, 0x6A1F, 0xE8A4 };

  for (int i = 0; i < GM_ITEMS; i++) {
    int y = GM_TOP + i * GM_STEP;
    box(10, y, SCR_W - 20, 34, i == GM_ITEMS - 1 ? C_PANEL2 : C_PANEL, C_LINE, 6);
    tft.fillCircle(26, y + 17, 4, acc[i]);
    textDraw(38, y + 9, items[i], F_SM, C_TXT);
    if (i < GM_ITEMS - 1) textRight(SCR_W - 22, y + 9, "›", F_MD, C_DIM);
  }
}

void gamesTouch(TouchPt t) {
  int i = (t.y - GM_TOP) / GM_STEP;
  if (i < 0 || i >= GM_ITEMS) return;
  switch (i) {
    case 0: go(&SCR_MEMORY); break;
    case 1: go(&SCR_REACT); break;
    case 2: go(&SCR_SIMON); break;
    case 3: go(&SCR_TICTAC); break;
    case 4: go(&SCR_DICE); break;
    case 5: go(&SCR_COIN); break;
    case 6: go(&SCR_GUESS); break;
    default: go(&SCR_LAUNCH); break;
  }
}

/* ══════════ УГАДАЙ ЦВЕТ ══════════ */
static uint8_t gcTarget = 0, gcTries = 0;
static String  gcOut = "плата загадала цвет";

void drawGuess() {
  topBack("УГАДАЙ ЦВЕТ", 18);
  textDraw(10, 52, gcOut.c_str(), F_SM, C_WARN);

  static const uint16_t COL[6] = { 0xC000, 0x0340, 0x0010, 0xB200, 0x6000, 0xFFFF };
  static const char *NAM[6] = { "красный", "зелёный", "синий", "оранжевый", "фиолетовый", "белый" };
  for (int i = 0; i < 6; i++) {
    int y = 78 + i * 33;
    box(10, y, SCR_W - 20, 28, COL[i], C_LINE, 6);
    textDraw(24, y + 6, NAM[i], F_SM, i == 5 ? C_TXT : C_WHITE);
  }
  if (gcTries) textDraw(10, 285, (String("попыток: ") + gcTries).c_str(), F_SM, C_DIM);
}

void guessTouch(TouchPt t) {
  if (t.y >= SCR_H - 38) { go(&SCR_GAMES); return; }
  int i = (t.y - 78) / 33;
  if (t.y < 78 || i < 0 || i > 5) return;
  gcTries++;
  if (gcTarget == 0xFF) { gcTarget = esp_random() % 6; gcTries = 1; }
  if (i == gcTarget) {
    gcOut = String("УГАДАЛ за ") + gcTries;
    neopixelWrite(RGB_PIN, 0, 220, 60);
    gcTarget = 0xFF;
  } else {
    gcOut = "не он, пробуй ещё";
    neopixelWrite(RGB_PIN, 60, 0, 0);
  }
  go(&SCR_GUESS);
}
