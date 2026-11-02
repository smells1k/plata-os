/* ══════════ УЧЁТНАЯ ЗАПИСЬ ══════════ */
/* Имя, аватарка, пароль на вход. Спрашивается один раз при включении
   (если пароль задан). Живёт в энергонезависимой памяти. */

String userName = "";
String userPass = "";
uint8_t userAva = 0;          /* 0..5 — какой значок рисуем */

uint8_t uAva() { return userAva; }
String  uName() { return userName; }

/* шесть простых аватарок: рисуем из примитивов */
static void avaDraw(int cx, int cy, int r, uint8_t kind) {
  uint16_t pal[6] = { C_ACC, C_OK, C_WARN, C_ERR, 0x8010, 0x2D7F };
  uint16_t c = pal[kind % 6];

  tft.drawCircle(cx, cy, r, c);
  tft.drawCircle(cx, cy, r - 1, c);
  switch (kind % 6) {
    case 0:                                     /* голова */
      tft.fillCircle(cx, cy - r / 3, r / 3, c);
      tft.fillRoundRect(cx - r / 2, cy + r / 8, r, r / 2, r / 3, c);
      break;
    case 1:                                     /* молния */
      tft.drawLine(cx + 4, cy - r + 4, cx - 5, cy + 1, c);
      tft.drawLine(cx - 5, cy + 1, cx + 1, cy + 1, c);
      tft.drawLine(cx + 1, cy + 1, cx - 4, cy + r - 4, c);
      tft.drawLine(cx - 4, cy + r - 4, cx + 5, cy - 1, c);
      tft.drawLine(cx + 5, cy - 1, cx - 1, cy - 1, c);
      break;
    case 2:                                     /* звезда */
      for (int i = 0; i < 6; i++) {
        float a = i * 3.14159f / 3;
        tft.fillCircle(cx + (int)(cos(a) * (r - 5)), cy + (int)(sin(a) * (r - 5)), 2, c);
      }
      tft.fillCircle(cx, cy, r / 3, c);
      break;
    case 3:                                     /* сердце */
      tft.fillCircle(cx - r / 4, cy - r / 5, r / 3, c);
      tft.fillCircle(cx + r / 4, cy - r / 5, r / 3, c);
      for (int i = 0; i <= r / 2; i++)
        tft.fillRect(cx - r / 2 + i, cy + i / 2, r - 2 * i, 3, c);
      break;
    case 4:                                     /* квадрат */
      tft.drawRect(cx - r / 2, cy - r / 2, r, r, c);
      tft.drawRect(cx - r / 2 + 2, cy - r / 2 + 2, r - 4, r - 4, c);
      break;
    default:                                    /* круг */
      tft.fillCircle(cx, cy, r / 2, c);
      break;
  }
}

/* ── экран учётной записи ── */
int g_usAva = 52, g_usName = 132, g_usPass = 176, g_usSave = 232;

/* рамка экрана: рисуем один раз, дальше только содержимое */
static void userBox(int y, int h) {
  tft.fillRect(8, y, SCR_W - 16, h, C_BG);       /* сначала стереть */
  box(8, y, SCR_W - 16, h, C_PANEL, C_LINE, 6);
}

void drawUser() {
  topBack("УЧЁТНАЯ ЗАПИСЬ", 16);

  userBox(50, 76);
  avaDraw(44, 88, 26, userAva);
  textDraw(80, 66, userAva ? "аватарка" : "без аватарки", F_SM, C_DIM);
  textDraw(80, 86, "нажми — сменить", F_SM, C_ACC);

  userBox(132, 38);
  textDraw(16, 136, "ИМЯ", F_SM, C_DIM);
  textDraw(16, 152, userName.length() ? textClip(userName, SCR_W - 36, F_SM).c_str()
                                      : "не задано", F_SM,
           userName.length() ? C_TXT : C_WARN);

  userBox(176, 44);
  textDraw(16, 182, "БЛОКИРОВКА", F_SM, C_DIM);
  textDraw(16, 199, "D4 — 5 секунд, или кнопка Питание", F_SM, C_TXT);

  btnDraw(8, 232, SCR_W - 16, 32, "СОХРАНИТЬ", C_OK, C_WHITE, F_MD);

  textCenter(SCR_H - 26, "аватарка рисуется на экране блокировки", F_SM, C_DIM);
}

void userTouch(TouchPt t) {
  if (topBackHit(t)) { go(&SCR_MORE); return; }

  if (t.y >= 50 && t.y < 126) { userAva = (userAva + 1) % 6; drawUser(); return; }

  if (t.y >= 132 && t.y < 170) {
    String b = userName;
    if (kbInput("ИМЯ", b)) userName = b;
    drawUser();
    return;
  }



  if (t.y >= 232 && t.y < 262) {
    cfgSaveUser();
    textCenter(SCR_H - 26, "сохранено", F_SM, C_OK);
    delay(800);
    go(&SCR_MORE);
    return;
  }
}

void cfgSaveUser() {
  cfg.begin("user", false);
  cfg.putString("name", userName);
  cfg.putString("pass", userPass);
  cfg.putUChar("ava", userAva);
  cfg.end();
}

void cfgLoadUser() {
  cfg.begin("user", true);
  userName = cfg.getString("name", "");
  userPass = cfg.getString("pass", "");
  userAva  = cfg.getUChar("ava", 0);
  cfg.end();
}

/* ── запрос пароля при включении ── */
bool askPassword() {
  return true;      /* вход без пароля: пароль не защищает ни от чего,
                       экран блокировки и так закрывается D4 */

  tft.fillScreen(C_BG);
  textCenter(90, "ПЛАТА-ОС", F_LG, C_WHITE);
  if (userName.length()) textCenter(122, userName.c_str(), F_MD, C_ACC);
  textCenter(160, "введи пароль", F_MD, C_DIM);

  String in = "";
  box(30, 200, SCR_W - 60, 40, C_PANEL, C_LINE, 8);
  textCenter(212, "", F_LG, C_TXT);

  while (true) {
    textCenter(212, (in.length() ? String(in.length(), '•') : String("_")).c_str(), F_LG, C_TXT);
    if (!kbInput("ПАРОЛЬ", in, false, true)) return false;
    if (in == userPass) { logAdd("вход с паролем"); return true; }
    tft.fillRect(0, 180, SCR_W, 80, C_BG);
    textCenter(190, "неверный пароль", F_MD, C_ERR);
    in = "";
    delay(900);
    tft.fillRect(0, 180, SCR_W, 80, C_BG);
  }
}
