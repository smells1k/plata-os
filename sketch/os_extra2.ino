/* ══════════ ЗАМЕТКА ══════════ */
/* Короткая записка на экране: список, молоко, что угодно. */

String noteText = "";

/* сколько строк и что в строке — для виджета на столе */
int noteLines() {
  if (!noteText.length()) return 0;
  return noteText.indexOf('\n') >= 0 ? 2 : 1;
}
String noteLine(int i) {
  int nl = noteText.indexOf('\n');
  if (i == 0) return nl >= 0 ? noteText.substring(0, nl) : noteText;
  return nl >= 0 ? noteText.substring(nl + 1) : String("");
}

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
