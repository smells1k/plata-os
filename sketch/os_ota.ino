/* ══════════ ОБНОВЛЕНИЕ ПРЯМО С GITHUB ══════════ */
/* Плата сама проверяет релизы и ставит новую прошивку:
   ПК и провод не нужны, только Wi-Fi.

   Берём последний релиз репозитория, ищем в нём .bin,
   качаем и пишем во второй раздел. Если что-то пошло не так —
   плата остаётся на старой прошивке. */

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>

static const char *GH_REPO = "smells1k/plata-os";


String   updMsg = "не проверял";
uint16_t updCol = C_DIM;
int      updPct = -1;
int      updKb  = 0;

String ghNotes = "";          /* что нового из релиза */

static float verNum(const String &v) {
  String s = v;
  if (s.startsWith("v") || s.startsWith("V")) s = s.substring(1);
  return s.toFloat();
}

/* значение поля из JSON: терпим и к пробелам вокруг двоеточия */
static String jsonField(const String &body, const char *key) {
  String pat = String("\"") + key + "\"";
  int i = body.indexOf(pat);
  if (i < 0) return "";
  int c = body.indexOf(':', i + pat.length());
  if (c < 0) return "";
  int a = body.indexOf('"', c);
  if (a < 0) return "";
  int b = body.indexOf('"', a + 1);
  if (b < 0) return "";
  return body.substring(a + 1, b);
}

/* ── проверка: есть ли новее ── */
bool otaCheck(bool quiet = false) {
  if (!wifiOk) {
    updMsg = "нужен Wi-Fi";
    updCol = C_WARN;
    return false;
  }
  if (!quiet) {
    updMsg = "проверяю…";
    updCol = C_DIM;
    updPct = -1;
    drawUpd();
  }

  WiFiClientSecure c;
  c.setInsecure();                       /* без проверки сертификата: экономия места */
  HTTPClient h;
  h.setTimeout(8000);
  h.setConnectTimeout(6000);
  String url = String("https://api.github.com/repos/") + GH_REPO + "/releases/latest";
  if (!h.begin(c, url)) {
    updMsg = "нет связи с GitHub";
    updCol = C_ERR;
    return false;
  }
  h.addHeader("Accept", "application/vnd.github+json");
  h.addHeader("User-Agent", "plata-os");
  int code = h.GET();
  if (code != 200) {
    h.end();
    updMsg = (code == 404) ? "релизов пока нет" : ("GitHub ответил " + String(code));
    updCol = C_WARN;
    return false;
  }
  String body = h.getString();
  h.end();

  ghTag = jsonField(body, "tag_name");
  ghNotes = "";                       /* что нового */
  {
    String bd = jsonField(body, "body");
    int p = bd.indexOf("Что");
    if (p < 0) p = bd.indexOf("НОВОЕ");
    if (p >= 0) {
      String line = bd.substring(p);
      int nl = line.indexOf('\n');
      if (nl > 0) line = line.substring(0, nl);
      line.trim();
      ghNotes = line;
    }
  }

  /* ищем ссылку на первый .bin в релизе */
  ghUrl = "";
  int i = 0;
  while (true) {
    int p = body.indexOf("\"browser_download_url\"", i);
    if (p < 0) break;
    int c = body.indexOf(':', p);
    if (c < 0) break;
    int a = body.indexOf('"', c);
    if (a < 0) break;
    int q = body.indexOf('"', a + 1);
    if (q < 0) break;
    String u = body.substring(a + 1, q);
    if (u.endsWith(".bin")) { ghUrl = u; break; }   /* файл один на всё */
    i = q;
  }

  if (!ghTag.length()) {
    updMsg = "на GitHub нет релиза";
    updCol = C_WARN;
    return false;
  }
  if (verNum(ghTag) > verNum(OS_VER) + 0.001f) {
    updMsg = ghTag + " · есть новая";
    updCol = C_OK;
  } else {
    updMsg = "у тебя свежая (" + ghTag + ")";
    updCol = C_OK;
  }
  return true;
}

/* ══════════ ПРИЁМНИК ОБРАЗА ══════════ */
/* Один и тот же файл годится и для кабеля, и для воздуха.
   Полный образ 4 МБ: первые 0x10000 — загрузчик и разметка, они уже есть.
   Конец программы виден по заполнению 0xFF. */
struct OtaSink {
  uint32_t skip  = 0;
  uint32_t total = 0;
  uint32_t seen  = 0;
  uint32_t writ  = 0;
  bool     stop  = false;

  bool begin(uint32_t totalBytes) {
    skip = 0; total = totalBytes; seen = 0; writ = 0; stop = false;
    skip = 0x10000UL;                    /* загрузчик и разметка уже в плате */
    return Update.begin(UPDATE_SIZE_UNKNOWN);
  }

  bool feed(const uint8_t *buf, size_t n) {
    if (stop) return false;
    size_t off = 0;
    if (seen < skip) {
      size_t drop = n;
      if (drop > skip - seen) drop = skip - seen;
      seen += drop;
      off = drop;
      if (off >= n) return true;
    }
    size_t put = n - off;

    bool fill = true;                      /* хвост образа добит 0xFF */
    for (size_t i = 0; i < put; i++)
      if (buf[off + i] != 0xFF) { fill = false; break; }
    if (fill && writ > 0x20000) { stop = true; return false; }

    if (Update.write((uint8_t *)buf + off, put) != put) { stop = true; return false; }
    seen += put;
    writ += put;
    return true;
  }

  int pct() const { return total ? (int)((uint64_t)seen * 100 / total) : 0; }
  uint32_t appLen() const { return total > skip ? total - skip : total; }

  bool finish() { return Update.end(true); }   /* размер = сколько записали */
};

/* ── установка ── */
/* GitHub иногда отдаёт файл кусками без указания размера.
   Тогда читаем до закрытия соединения — главное, чтобы байты шли. */
bool otaInstall() {
  if (!ghUrl.length()) {
    if (!otaCheck()) { drawUpd(); return false; }
  }
  if (!ghUrl.length()) {
    updMsg = "в релизе нет .bin";
    updCol = C_ERR;
    drawUpd();
    return false;
  }

  updMsg = "скачиваю…";
  updCol = C_ACC;
  updPct = -1;
  updKb = 0;
  drawUpd();

  WiFiClientSecure c;
  c.setInsecure();
  HTTPClient h;
  h.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  h.setTimeout(20000);
  h.setConnectTimeout(10000);
  if (!h.begin(c, ghUrl)) {
    updMsg = "не открыть файл";
    updCol = C_ERR;
    drawUpd();
    return false;
  }
  h.addHeader("User-Agent", "plata-os");
  h.addHeader("Accept", "*/*");
  int code = h.GET();
  if (code != 200) {
    h.end();
    updMsg = "GitHub: код " + String(code);
    updCol = C_ERR;
    drawUpd();
    return false;
  }

  int len = h.getSize();                 /* может быть -1: файл отдают кусками */

  static OtaSink sink;
  if (!sink.begin(len > 0 ? (uint32_t)len : 4194304UL)) {
    h.end();
    updMsg = "не влезает в память";
    updCol = C_ERR;
    drawUpd();
    return false;
  }

  WiFiClient *st = h.getStreamPtr();
  uint8_t buf[1024];
  uint32_t limit = (len > 0) ? (uint32_t)len : 4194304UL;
  int lastShown = -1;
  uint32_t t0 = millis();
  bool stuck = false;

  while (sink.seen < limit && !sink.stop) {
    size_t avail = st->available();
    if (avail == 0) {
      if (!h.connected() && st->available() == 0) break;   /* всё пришло */
      if (millis() - t0 > 20000) { stuck = true; break; }
      delay(1);
      continue;
    }
    size_t want = avail;
    if (want > sizeof(buf)) want = sizeof(buf);
    if (sink.seen + want > limit) want = limit - sink.seen;

    size_t n = st->read(buf, want);
    if (n == 0) { if (millis() - t0 > 20000) { stuck = true; break; } delay(1); continue; }
    t0 = millis();
    if (!sink.feed(buf, n)) break;

    /* экран обновляем редко — иначе не успеваем читать сеть */
    updKb = (int)(sink.writ / 1024);
    int p = (len > 0) ? sink.pct() : -1;
    int show = (len > 0) ? (p / 3) * 3 : updKb / 40;
    if (show != lastShown) { lastShown = show; updPct = p; drawUpd(); }
  }
  h.end();

  if (stuck) {
    Update.abort();
    updMsg = "связь подвисла на " + String(updKb) + " КБ";
    updCol = C_ERR;
    updPct = -1;
    drawUpd();
    return false;
  }

  if (sink.writ > 0 && sink.finish()) {
    updPct = 100;
    updMsg = "готово, перезапускаюсь";
    updCol = C_OK;
    drawUpd();
    delay(1200);
    ESP.restart();
  }

  Update.abort();
  updMsg = "записалось " + String(updKb) + " КБ — не хватило места";
  updCol = C_ERR;
  updPct = -1;
  drawUpd();
  return false;
}

/* ── экран ── */
void drawUpd() {
  topBack("ОБНОВЛЕНИЕ", 16);

  /* ── две строки версий ── */
  box(8, 52, SCR_W - 16, 26, C_PANEL, C_LINE, 5);
  textDraw(14, 57, "у тебя", F_SM, C_DIM);
  textRight(SCR_W - 14, 57, OS_VER, F_MD, C_TXT);

  box(8, 82, SCR_W - 16, 26, C_PANEL, C_LINE, 5);
  textDraw(14, 87, "на GitHub", F_SM, C_DIM);
  textRight(SCR_W - 14, 87, ghTag.length() ? ghTag.c_str() : "—", F_MD,
            ghTag.length() ? C_ACC : C_DIM);

  /* ── состояние: одна строка, всегда на своём месте ── */
  tft.fillRect(0, 112, SCR_W, 24, C_BG);
  textCenter(116, textClip(updMsg, SCR_W - 12, F_MD).c_str(), F_MD, updCol);

  /* ── подробность ── */
  tft.fillRect(0, 138, SCR_W, 20, C_BG);
  if (updPct >= 0)
    textCenter(140, (String(updPct) + "%  ·  " + String(updKb) + " КБ").c_str(), F_SM, C_DIM);
  else if (updKb > 0)
    textCenter(140, (String(updKb) + " КБ принято").c_str(), F_SM, C_DIM);
  else if (ghNotes.length())
    textCenter(140, textClip(ghNotes, SCR_W - 12, F_SM).c_str(), F_SM, C_DIM);
  else
    textCenter(140, wifiOk ? "нужно только Wi-Fi" : "нужен Wi-Fi", F_SM, C_DIM);

  /* ── полоса прогресса (своя зона, ничего не задевает) ── */
  int w = SCR_W - 40;
  tft.drawRect(20, 166, w, 18, C_LINE);
  if (updPct > 0) tft.fillRect(22, 168, (w - 4) * updPct / 100, 14, C_ACC);

  /* ── куда стучимся ── */
  textCenter(198, "плата качает с GitHub", F_SM, C_DIM);
  textCenter(216, GH_REPO, F_SM, C_ACC);

  btnDraw(8, SCR_H - 66, SCR_W - 16, 28, "ПРОВЕРИТЬ", C_PANEL, C_TXT, F_SM);
  btnDraw(8, SCR_H - 34, SCR_W - 16, 28, "УСТАНОВИТЬ С GITHUB", C_ACC, C_WHITE, F_SM);
}

void updTouch(TouchPt t) {
  if (topBackHit(t)) { go(&SCR_MORE); return; }
  if (t.y >= SCR_H - 34) { otaInstall(); return; }
  if (t.y >= SCR_H - 68 && t.y < SCR_H - 38) { otaCheck(); drawUpd(); return; }
}
