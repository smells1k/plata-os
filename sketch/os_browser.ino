/* ══════════ БРАУЗЕР (текстовый) ══════════ */
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
/* Полноценный браузер с картинками и скриптами на ESP32 не помещается.
   Этот умеет главное: открыть адрес, вытащить из страницы текст,
   ссылки и заголовки, и листать его пальцем. Как Lynx. */

/* веб-сервер нужен в обоих режимах: пульт, обновления */
#if !HAVE_OTA
#undef HAVE_OTA
#define HAVE_OTA 1
#endif

String brUrl  = "";          /* что открыто */
String brText = "";          /* текст страницы, уже разбитый на строки */
int    brTop  = 0;           /* с какой строки показываем */
int    brLines = 0;          /* всего строк */
uint16_t brCol = C_TXT;

#define BR_VIS   13          /* строк на экране */
#define BR_MAX   7000        /* больше в память не лезет */

/* служебные символы html → текст */
static void brDecode(String &s) {
  s.replace("&nbsp;", " ");   s.replace("&amp;", "&");
  s.replace("&lt;", "<");     s.replace("&gt;", ">");
  s.replace("&quot;", "\"");  s.replace("&#39;", "'");
  s.replace("&laquo;", "«");  s.replace("&raquo;", "»");
  s.replace("&mdash;", "—");  s.replace("&ndash;", "–");
  s.replace("&hellip;", "…");
}

/* выкидываем теги, оставляя текст и переводы строк */
static String brStrip(const String &in) {
  String out;
  out.reserve(BR_MAX);
  bool inTag = false, inScript = false;
  for (unsigned int i = 0; i < in.length() && out.length() < BR_MAX; i++) {
    char c = in[i];
    if (!inTag && c == '<') {
      inTag = true;
      /* script/style целиком пропускаем */
      if (in.substring(i, i + 7) == "<script") inScript = true;
      if (in.substring(i, i + 8) == "</script") inScript = false;
      /* блочные теги — как перевод строки */
      String t = in.substring(i, i + 3);
      if (t == "<p>" || t == "<br" || in.substring(i, i + 3) == "<li"
          || in.substring(i, i + 2) == "<h1" || in.substring(i, i + 2) == "<h2"
          || in.substring(i, i + 2) == "<h3" || in.substring(i, i + 4) == "<div")
        out += '\n';
      continue;
    }
    if (inTag) { if (c == '>') inTag = false; continue; }
    if (inScript) continue;
    if (c == '\n' || c == '\r' || c == '\t') { out += ' '; continue; }
    out += c;
  }
  /* лишние пробелы и пустые строки */
  String clean;
  clean.reserve(out.length());
  bool sp = false, nl = false;
  for (unsigned int i = 0; i < out.length(); i++) {
    char c = out[i];
    if (c == ' ') { if (!sp && !nl) clean += c; sp = true; continue; }
    if (c == '\n') { if (!nl && clean.length() > 1) clean += c; sp = false; nl = true; continue; }
    sp = false; nl = false;
    clean += c;
  }
  brDecode(clean);
  return clean;
}

/* разбиваем на строки по ширине экрана */
static void brWrap(const String &src) {
  brText = "";
  brText.reserve(src.length() + 200);
  int lim = SCR_W - 20;
  int start = 0;
  while (start < (int)src.length()) {
    int nl = src.indexOf('\n', start);
    if (nl < 0) nl = src.length();
    String para = src.substring(start, nl);
    start = nl + 1;
    para.trim();
    if (!para.length()) { brText += '\n'; continue; }
    while (para.length()) {
      int cut = para.length();
      while (cut > 1 && textW(para.substring(0, cut).c_str(), F_SM) > lim) cut--;
      if (cut < (int)para.length()) {
        int sp = para.lastIndexOf(' ', cut);
        if (sp > 0) cut = sp;
      }
      brText += para.substring(0, cut);
      brText += '\n';
      para = para.substring(cut);
      para.trim();
      if (brText.length() > BR_MAX) return;
    }
  }
  brLines = 0;
  for (unsigned int i = 0; i < brText.length(); i++) if (brText[i] == '\n') brLines++;
}

static String brLine(int i) {
  int pos = 0, n = 0;
  while (pos < (int)brText.length() && n < i) {
    int e = brText.indexOf('\n', pos);
    if (e < 0) return "";
    pos = e + 1; n++;
  }
  int e = brText.indexOf('\n', pos);
  if (e < 0) e = brText.length();
  return brText.substring(pos, e);
}

/* ── экран ── */
void drawBrowser() {
  topBack("БРАУЗЕР", 16);

  /* адрес */
  box(8, 52, SCR_W - 16, 26, C_PANEL, C_LINE, 5);
  textDraw(14, 58, brUrl.length() ? textClip(brUrl, SCR_W - 28, F_SM).c_str() : "нажми, чтобы ввести адрес",
           F_SM, brUrl.length() ? C_ACC : C_DIM);

  /* текст страницы */
  tft.fillRect(4, 84, SCR_W - 8, SCR_H - 84 - 40, C_BG);
  if (!brLines) {
    textCenter(140, "тут будет страница", F_MD, C_DIM);
    textCenter(168, "введи адрес сверху", F_SM, C_DIM);
    textCenter(196, "например 2ip.ru", F_SM, C_ACC);
  } else {
    for (int i = 0; i < BR_VIS; i++) {
      int li = brTop + i;
      if (li >= brLines) break;
      String s = brLine(li);
      if (!s.length()) continue;
      textDraw(10, 88 + i * 14, s.c_str(), F_SM, C_TXT);
    }
    /* полоса прокрутки справа */
    if (brLines > BR_VIS) {
      int h = (SCR_H - 124) * BR_VIS / brLines;
      int y = 88 + (SCR_H - 124 - h) * brTop / (brLines - BR_VIS);
      tft.fillRoundRect(SCR_W - 4, y, 3, h, 1, C_ACC);
    }
  }

  /* кнопки */
  btnDraw(8, SCR_H - 34, 74, 28, "ВВЕРХ", C_PANEL2, C_TXT, F_SM);
  btnDraw(86, SCR_H - 34, 74, 28, "ВНИЗ", C_PANEL2, C_TXT, F_SM);
  btnDraw(164, SCR_H - 34, 68, 28, "АДРЕС", C_ACC, C_WHITE, F_SM);
}

void browserTouch(TouchPt t) {
  if (topBackHit(t)) { go(&SCR_MORE); return; }

  if (t.y >= SCR_H - 34) {
    if (t.x < 84) { brTop -= BR_VIS; if (brTop < 0) brTop = 0; go(&SCR_BR); }
    else if (t.x < 162) {
      brTop += BR_VIS;
      if (brTop > brLines - 1) brTop = brLines > BR_VIS ? brLines - BR_VIS : 0;
      go(&SCR_BR);
    } else {
      String u = brUrl;
      if (kbInput("АДРЕС (сайт или IP)", u)) {
        u.trim();
        if (u.length()) {
          if (!u.startsWith("http")) u = "http://" + u;
          brUrl = u;
          brOpen();
        }
      }
      go(&SCR_BR);
    }
    return;
  }

  /* касание адресной строки */
  if (t.y >= 52 && t.y < 80) {
    String u = brUrl;
    if (kbInput("АДРЕС (сайт или IP)", u)) {
      u.trim();
      if (u.length()) {
        if (!u.startsWith("http")) u = "http://" + u;
        brUrl = u;
        brOpen();
      }
    }
    go(&SCR_BR);
  }

  /* листание касанием по тексту */
  if (t.y >= 84 && t.y < SCR_H - 40) {
    if (t.x < SCR_W / 2) { brTop -= 5; if (brTop < 0) brTop = 0; }
    else { brTop += 5; if (brTop > brLines - 1) brTop = brLines > BR_VIS ? brLines - BR_VIS : 0; }
    go(&SCR_BR);
  }
}

/* ── загрузка страницы ── */
void brOpen() {
  brText = ""; brLines = 0; brTop = 0;
  tft.fillScreen(C_BG);
  statusDraw();
  textCenter(120, "открываю…", F_MD, C_DIM);
  textCenter(146, textClip(brUrl, SCR_W - 30, F_SM).c_str(), F_SM, C_ACC);

  if (!wifiOk) {
    textCenter(190, "нужен Wi-Fi", F_MD, C_ERR);
    textCenter(216, "своя сеть — тоже без инета", F_SM, C_DIM);
    delay(1600);
    go(&SCR_BR);
    return;
  }

  bool https = brUrl.startsWith("https");
  HTTPClient h;
  h.setTimeout(12000);
  h.setConnectTimeout(8000);
  h.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  h.addHeader("User-Agent", "Mozilla/5.0 (PLATA-OS)");

  WiFiClientSecure sec;
  WiFiClient plain;
  bool ok;
  if (https) { sec.setInsecure(); ok = h.begin(sec, brUrl); }
  else       { ok = h.begin(plain, brUrl); }
  if (!ok) {
    textCenter(190, "адрес не принят", F_MD, C_ERR);
    delay(1400);
    go(&SCR_BR);
    return;
  }

  int code = h.GET();
  if (code != 200) {
    h.end();
    textCenter(190, ("ответ " + String(code)).c_str(), F_MD, C_ERR);
    delay(1400);
    go(&SCR_BR);
    return;
  }

  String body = h.getString();
  h.end();

  if (!body.length()) {
    textCenter(190, "пустая страница", F_MD, C_WARN);
    delay(1400);
    go(&SCR_BR);
    return;
  }

  String plainText = brStrip(body);
  brWrap(plainText);
  brTop = 0;
  logAdd("открыт сайт");
  go(&SCR_BR);
}
