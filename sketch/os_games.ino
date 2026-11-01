/*
 *  os_games.ino — игры и мелочи для ПЛАТА-ОС
 *  Подключается из board-os.ino, использует его переменные и UI-ядро.
 */

/* ══════════ ПРОВЕРЬ ПАМЯТЬ ══════════ */
#define MEM_LEVELS 12
static uint16_t memLevel = 0;
static uint8_t  memSeq[24];
static uint8_t  memPos = 0;
static int      memCell[6] = { -1, -1, -1, -1, -1, -1 };   /* индексы в сетке */
static bool     memShow = false;

static String memStatus = "нажми «Старт»";

void drawMemory() {
  topBack("ПРОВЕРЬ ПАМЯТЬ", 18);
  textDraw(10, 50, memStatus.c_str(), F_SM, memPos ? C_WARN : C_DIM);

  /* сетка 3x2 */
  for (int i = 0; i < 6; i++) {
    int cx = 12 + (i % 3) * 74, cy = 78 + (i / 3) * 74;
    bool lit = false;
    for (int k = 0; k < (int)memLevel && k < 24; k++) if (memSeq[k] == i) lit = true;
    uint16_t c = lit ? (memShow ? 0x3666 : C_ACC) : C_PANEL;
    box(cx, cy, 68, 66, c, lit ? C_WHITE : C_LINE, 8);
  }
  textDraw(10, 240, (String("уровень ") + (memLevel ? memLevel : 1) + " · найдено " + memPos).c_str(), F_SM, C_DIM);

  btnDraw(10, SCR_H - 38, 106, 30, "СТАРТ", C_ACC, C_WHITE, F_SM);
}

static void memStart() {
  memLevel = 2;
  memPos = 0;
  memStatus = "запоминай…";
  memShow = false;
  for (int i = 0; i < 24; i++) memSeq[i] = esp_random() % 6;
  go(&SCR_MEMORY);
  delay(500);
  memShow = true;
  go(&SCR_MEMORY);
  delay(200 + memLevel * 650);
  memShow = false;
  memStatus = "повтори";
  go(&SCR_MEMORY);
}

void memoryTouch(TouchPt t) {
  if (topBackHit(t)) { go(&SCR_GAMES); return; }
  if (t.y >= SCR_H - 38) {
    if (t.x < 120) { memStart(); }
    else go(&SCR_GAMES);
    return;
  }
  if (t.y < 132) return;                 /* в шапке не тыкаем */
  if (memLevel == 0 || memShow) return;

  int col = (t.x - 12) / 74, row = (t.y - 78) / 74;
  if (col < 0 || col > 2 || row < 0 || row > 1) return;
  int idx = row * 3 + col;

  if (idx == memSeq[memPos]) {
    memPos++;
    if (memPos >= memLevel) {
      if (memLevel >= MEM_LEVELS) {
        memStatus = "ВСЁ! Память супер";
        memLevel = 0;
        go(&SCR_MEMORY);
        return;
      }
      memLevel += 2;
      memPos = 0;
      memStatus = "уровень пройден!";
      go(&SCR_MEMORY);
      delay(700);
      memStart();
      return;
    }
    memStatus = "верно";
  } else {
    memStatus = String("ошибка на уровне ") + memLevel;
    for (int k = 0; k < 3; k++) { neopixelWrite(RGB_PIN, 200, 0, 0); delay(140); neopixelWrite(RGB_PIN, 0, 0, 0); delay(100); }
    memLevel = 0;
    memPos = 0;
  }
  go(&SCR_MEMORY);
}

/* ══════════ РЕАКЦИЯ ══════════ */
static uint32_t reactStart = 0;
static int      reactState = 0;      /* 0 ждём, 1 зелёный, 2 результат, 3 рано */
static uint32_t reactBest = 0;
static String   reactOut = "нажми «Старт»";

void drawReact() {
  topBack("РЕАКЦИЯ", 20);

  uint16_t boxc = reactState == 1 ? 0x3666 : reactState == 3 ? C_ERR : C_PANEL;
  box(10, 60, SCR_W - 20, 150, boxc, C_LINE, 10);
  String big = reactOut;
  headline(24, 108, big.c_str(), C_WHITE, 24);
  if (reactBest) textDraw(10, 222, (String("рекорд ") + reactBest + " мс").c_str(), F_SM, C_DIM);

  btnDraw(10, SCR_H - 38, SCR_W - 20, 30, reactState == 1 ? "ЖМИ!" : "СТАРТ", C_ACC, C_WHITE, F_SM);
}

void reactTouch(TouchPt t) {
  if (t.y >= SCR_H - 38) {
    reactState = 0;
    reactOut = "жди зелёного…";
    go(&SCR_REACT);
    delay(1200 + esp_random() % 3000);
    reactState = 1;
    reactStart = millis();
    reactOut = "ЖМИ!";
    neopixelWrite(RGB_PIN, 0, 200, 0);
    go(&SCR_REACT);
    return;
  }
  if (reactState == 1) {
    uint32_t ms = millis() - reactStart;
    if (!reactBest || ms < reactBest) reactBest = ms;
    reactState = 2;
    reactOut = String(ms) + " мс";
    neopixelWrite(RGB_PIN, 0, 0, 0);
    go(&SCR_REACT);
  } else if (reactState == 0 && reactOut.startsWith("жди")) {
    reactState = 3;
    reactOut = "рано!";
    go(&SCR_REACT);
  }
}

/* ══════════ КУБИК ══════════ */
static String diceOut = "—";

void drawDice() {
  topBack("КУБИК", 20);
  box(30, 70, 180, 150, C_PANEL, C_LINE, 12);
  headline(90, 120, diceOut.c_str(), C_WHITE, 44);
  btnDraw(10, SCR_H - 90, SCR_W - 20, 36, "БРОСИТЬ", C_ACC, C_WHITE, F_MD);
  btnDraw(10, SCR_H - 44, SCR_W - 20, 34, "НАЗАД В ИГРЫ", C_PANEL, C_TXT, F_SM);
}

void diceTouch(TouchPt t) {
  if (topBackHit(t)) { go(&SCR_GAMES); return; }
  if (t.y >= SCR_H - 44) { go(&SCR_GAMES); return; }
  if (t.y >= SCR_H - 90 && t.y < SCR_H - 50) {
    for (int i = 0; i < 8; i++) {
      neopixelWrite(RGB_PIN, 40 + i * 25, 40, 200);
      delay(70);
    }
    static const char *FACE[7] = { "", "1", "2", "3", "4", "5", "6" };
    static const uint8_t COLS[7][3] = { {0,0,0},{200,0,0},{0,180,60},{0,60,200},{200,160,0},{160,0,180},{0,180,180} };
    int v = 1 + esp_random() % 6;
    diceOut = FACE[v];
    neopixelWrite(RGB_PIN, COLS[v][0], COLS[v][1], COLS[v][2]);
    go(&SCR_DICE);
  }
}

/* ══════════ МОНЕТКА ══════════ */
static String coinOut = "—";

void drawCoin() {
  topBack("МОНЕТКА", 20);
  box(30, 70, 180, 150, C_PANEL, C_LINE, 12);
  tft.fillCircle(120, 145, 46, C_PANEL2);
  tft.drawCircle(120, 145, 46, C_LINE);
  headline(78, 132, coinOut.c_str(), C_WHITE, 22);
  btnDraw(10, SCR_H - 90, SCR_W - 20, 36, "БРОСИТЬ", C_ACC, C_WHITE, F_MD);
  btnDraw(10, SCR_H - 44, SCR_W - 20, 34, "НАЗАД В ИГРЫ", C_PANEL, C_TXT, F_SM);
}

void coinTouch(TouchPt t) {
  if (topBackHit(t)) { go(&SCR_GAMES); return; }
  if (t.y >= SCR_H - 44) { go(&SCR_GAMES); return; }
  if (t.y >= SCR_H - 90 && t.y < SCR_H - 50) {
    for (int i = 0; i < 10; i++) {
      neopixelWrite(RGB_PIN, i % 2 ? 200 : 0, i % 2 ? 160 : 0, i % 2 ? 0 : 200);
      delay(70);
    }
    bool heads = esp_random() % 2;
    coinOut = heads ? "ОРЁЛ" : "РЕШКА";
    if (heads) neopixelWrite(RGB_PIN, 200, 160, 0); else neopixelWrite(RGB_PIN, 120, 0, 200);
    go(&SCR_COIN);
  }
}

/* ══════════ САЙМОН ══════════ */
static uint8_t simSeq[16];
static int     simLen = 3, simStep = 0, simLevel = 1, simBest = 0;
static bool    simInput = false, simPlaying = false;
static String  simOut = "нажми «Старт»";

void drawSimon() {
  topBack("САЙМОН", 20);
  textDraw(10, 52, simOut.c_str(), F_SM, C_WARN);

  /* три кнопки-цвета */
  static const uint16_t COL[3] = { 0xC000, 0x0340, 0x0010 };
  for (int i = 0; i < 3; i++) {
    int y = 80 + i * 56;
    box(10, y, SCR_W - 20, 50, COL[i], C_LINE, 8);
    const char *lbl = i == 0 ? "КРАСНЫЙ" : i == 1 ? "ЗЕЛЁНЫЙ" : "СИНИЙ";
    textDraw(24, y + 18, lbl, F_MD, C_WHITE);
  }
  textDraw(10, 252, (String("уровень ") + simLevel + (simBest ? String(" · рекорд ") + simBest : String(""))).c_str(), F_SM, C_DIM);

  btnDraw(10, SCR_H - 38, 106, 30, "СТАРТ", C_ACC, C_WHITE, F_SM);
}

static void simonShow() {
  simInput = false;
  simOut = "смотри…";
  go(&SCR_SIMON);
  delay(500);
  static const uint8_t C[3][3] = { {200,0,0},{0,200,40},{0,60,220} };
  for (int i = 0; i < simLen; i++) {
    uint8_t c = simSeq[i];
    neopixelWrite(RGB_PIN, C[c][0], C[c][1], C[c][2]);
    delay(400);
    neopixelWrite(RGB_PIN, 0, 0, 0);
    delay(180);
  }
  simStep = 0;
  simInput = true;
  simOut = "повтори";
  go(&SCR_SIMON);
}

static void simonStart() {
  simLen = 3; simLevel = 1; simStep = 0;
  for (int i = 0; i < 16; i++) simSeq[i] = esp_random() % 3;
  simonShow();
}

void simonTouch(TouchPt t) {
  if (topBackHit(t)) { go(&SCR_GAMES); return; }
  if (t.y >= SCR_H - 38) {
    if (t.x < 120) simonStart();
    else go(&SCR_GAMES);
    return;
  }
  if (!simInput) return;
  int idx = (t.y - 80) / 56;
  if (t.y < 80 || idx < 0 || idx > 2) return;

  static const uint8_t C[3][3] = { {200,0,0},{0,200,40},{0,60,220} };
  neopixelWrite(RGB_PIN, C[idx][0], C[idx][1], C[idx][2]);
  delay(160);
  neopixelWrite(RGB_PIN, 0, 0, 0);

  if (idx == simSeq[simStep]) {
    simStep++;
    if (simStep >= simLen) {
      simInput = false;
      if (simLevel > simBest) simBest = simLevel;
      simOut = "верно!";
      go(&SCR_SIMON);
      delay(600);
      simLevel++;
      simLen = min(simLen + 1, 16);
      simonShow();
    }
  } else {
    simInput = false;
    simOut = String("ошибка на ") + simLevel;
    for (int k = 0; k < 4; k++) { neopixelWrite(RGB_PIN, 220, 0, 0); delay(140); neopixelWrite(RGB_PIN, 0, 0, 0); delay(110); }
    simLevel = 1;
    go(&SCR_SIMON);
  }
}

/* ══════════ КРЕСТИКИ-НОЛИКИ ══════════ */
static uint8_t tt[9];
static bool    ttOver = false;
static String  ttOut = "твой ход · ты крестик";

static int ttWinner() {
  static const uint8_t L[8][3] = {
    {0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6}
  };
  for (int i = 0; i < 8; i++) {
    uint8_t a = tt[L[i][0]], b = tt[L[i][1]], c = tt[L[i][2]];
    if (a && a == b && b == c) return a;
  }
  for (int i = 0; i < 9; i++) if (!tt[i]) return 0;
  return 3;          /* ничья */
}

static void ttDrawBoard() {
  /* поле 3x3 */
  for (int i = 0; i < 9; i++) {
    int cx = 14 + (i % 3) * 72, cy = 84 + (i / 3) * 72;
    box(cx, cy, 66, 66, C_PANEL, C_LINE, 8);
    int mx = cx + 33, my = cy + 33;
    if (tt[i] == 1) {                     /* крестик */
      tft.drawLine(mx - 16, my - 16, mx + 16, my + 16, C_ACC);
      tft.drawLine(mx - 16, my + 16, mx + 16, my - 16, C_ACC);
      tft.drawLine(mx - 15, my - 16, mx + 17, my + 16, C_ACC);
      tft.drawLine(mx - 17, my + 16, mx + 15, my - 16, C_ACC);
    } else if (tt[i] == 2) {              /* нолик */
      tft.drawCircle(mx, my, 15, C_ERR);
      tft.drawCircle(mx, my, 14, C_ERR);
      tft.drawCircle(mx, my, 16, C_ERR);
    }
  }
}

void drawTicTac() {
  topBack("КРЕСТИКИ-НОЛИКИ", 16);
  textDraw(10, 52, ttOut.c_str(), F_SM, C_WARN);
  ttDrawBoard();
  btnDraw(10, SCR_H - 38, SCR_W - 20, 30, "ЗАНОВО", C_ACC, C_WHITE, F_SM);
}

static void ttAI() {
  /* 1) выигрышный ход 2) блок 3) центр 4) угол 5) любой */
  for (int who = 2; who >= 1; who--) {
    for (int i = 0; i < 9; i++) {
      if (tt[i]) continue;
      tt[i] = who;
      if (ttWinner() == who) { if (who == 2) return; tt[i] = 0; }
      else tt[i] = 0;
    }
  }
  if (!tt[4]) { tt[4] = 2; return; }
  static const uint8_t CORNERS[4] = { 0, 2, 6, 8 };
  for (int i = 0; i < 4; i++) if (!tt[CORNERS[i]]) { tt[CORNERS[i]] = 2; return; }
  for (int i = 0; i < 9; i++) if (!tt[i]) { tt[i] = 2; return; }
}

void ttTouch(TouchPt t) {
  if (t.y >= SCR_H - 38) {
    for (int i = 0; i < 9; i++) tt[i] = 0;
    ttOver = false;
    ttOut = "твой ход · ты крестик";
    go(&SCR_TICTAC);
    return;
  }
  if (ttOver) return;
  int col = (t.x - 14) / 72, row = (t.y - 84) / 72;
  if (col < 0 || col > 2 || row < 0 || row > 1 + 1) return;
  int idx = row * 3 + col;
  if (idx > 8 || tt[idx]) return;

  tt[idx] = 1;
  int w = ttWinner();
  if (w == 1) { ttOut = "ты выиграл!"; ttOver = true; go(&SCR_TICTAC); return; }
  if (w == 3) { ttOut = "ничья"; ttOver = true; go(&SCR_TICTAC); return; }

  ttAI();
  w = ttWinner();
  if (w == 2) { ttOut = "я выиграл"; ttOver = true; }
  else if (w == 3) { ttOut = "ничья"; ttOver = true; }
  else ttOut = "твой ход";
  go(&SCR_TICTAC);
}
