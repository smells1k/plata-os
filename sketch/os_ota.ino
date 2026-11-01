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
    updMsg = code == 404 ? "релизов пока нет" : ("GitHub ответил " + String(code));
    updCol = C_WARN;
    return false;
  }
  String body = h.getString();
  h.end();

  ghTag = jsonField(body, "tag_name");

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
    updMsg = "релизов пока нет";
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

/* ── прогресс: перерисовываем только полосу ── */
static void updBar() {
  int w = SCR_W - 60;
  tft.fillRect(30, 176, w, 26, C_BG);
  tft.drawRect(30, 176, w, 24, C_LINE);
  if (updPct > 0) tft.fillRect(32, 178, (w - 4) * updPct / 100, 20, C_ACC);
  tft.fillRect(70, 206, 100, 22, C_BG);
  textCenter(210, (String(updPct) + "%").c_str(), F_MD, C_TXT);
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
    total = totalBytes;
    skip  = (totalBytes > 0x180000UL) ? 0x10000UL : 0;
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

  bool finish() { return Update.end(true); }   /* размер = сколько записали */
};

/* ── установка ── */
bool otaInstall() {
  if (!ghUrl.length()) {
    if (!otaCheck()) { drawUpd(); return false; }
  }
  if (!ghUrl.length()) {
    updMsg = "в релизе нет файла";
    updCol = C_ERR;
    drawUpd();
    return false;
  }

  updMsg = "скачиваю…";
  updCol = C_ACC;
  updPct = 0;
  drawUpd();

  WiFiClientSecure c;
  c.setInsecure();
  HTTPClient h;
  h.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  h.setTimeout(15000);
  h.setConnectTimeout(8000);
  if (!h.begin(c, ghUrl)) {
    updMsg = "не открыть файл";
    updCol = C_ERR;
    drawUpd();
    return false;
  }
  h.addHeader("User-Agent", "plata-os");
  int code = h.GET();
  if (code != 200) {
    h.end();
    updMsg = "файл: код " + String(code);
    updCol = C_ERR;
    drawUpd();
    return false;
  }
  int len = h.getSize();
  if (len <= 0) {
    h.end();
    updMsg = "не знаю размер файла";
    updCol = C_ERR;
    drawUpd();
    return false;
  }

  static OtaSink sink;
  if (!sink.begin((uint32_t)len)) {
    h.end();
    updMsg = "не влезает в память";
    updCol = C_ERR;
    drawUpd();
    return false;
  }

  WiFiClient *st = h.getStreamPtr();
  uint8_t buf[1024];
  int lastPct = -1;
  uint32_t t0 = millis();

  while (sink.seen < (uint32_t)len && !sink.stop) {
    size_t want = (size_t)len - sink.seen;
    if (want > sizeof(buf)) want = sizeof(buf);
    size_t n = st->read(buf, want);
    if (n == 0) {
      if (!h.connected() && st->available() == 0) break;
      if (millis() - t0 > 20000) break;      /* зависло — выходим */
      delay(2);
      continue;
    }
    t0 = millis();
    if (!sink.feed(buf, n)) break;

    int p = sink.pct();
    if (p != lastPct) { lastPct = p; updPct = p; updBar(); }
  }
  h.end();

  if ((sink.stop || sink.seen >= (uint32_t)len) && sink.writ > 0 && sink.finish()) {
    updPct = 100;
    updBar();
    updMsg = "готово, перезапускаюсь";
    updCol = C_OK;
    drawUpd();
    delay(1200);
    ESP.restart();
  }

  Update.abort();
  updMsg = "оборвалось на " + String(sink.writ / 1024) + " КБ";
  updCol = C_ERR;
  updPct = -1;
  drawUpd();
  return false;
}

/* ── экран ── */
void drawUpd() {
  topBack("ОБНОВЛЕНИЕ", 18);

  box(10, 48, SCR_W - 20, 32, C_PANEL, C_LINE, 6);
  textDraw(18, 52, "У ТЕБЯ", F_SM, C_DIM);
  textRight(SCR_W - 18, 52, OS_VER, F_MD, C_TXT);

  box(10, 84, SCR_W - 20, 32, C_PANEL, C_LINE, 6);
  textDraw(18, 88, "НА GITHUB", F_SM, C_DIM);
  textRight(SCR_W - 18, 88, ghTag.length() ? ghTag.c_str() : "—", F_MD,
            ghTag.length() ? C_ACC : C_DIM);

  textCenter(126, textClip(updMsg, SCR_W - 20, F_SM).c_str(), F_SM, updCol);

  /* полоса прогресса */
  int w = SCR_W - 60;
  tft.drawRect(30, 176, w, 24, C_LINE);
  if (updPct > 0) tft.fillRect(32, 178, (w - 4) * updPct / 100, 20, C_ACC);
  if (updPct >= 0) textCenter(210, (String(updPct) + "%").c_str(), F_MD, C_TXT);
  else textCenter(238, wifiOk ? "один файл: и для кабеля, и для воздуха" : "нужен Wi-Fi",
                  F_SM, C_DIM);

  btnDraw(10, SCR_H - 68, SCR_W - 20, 30, "Проверить обновление", C_PANEL, C_TXT, F_SM);
  btnDraw(10, SCR_H - 34, SCR_W - 20, 30, "Установить с GitHub", C_ACC, C_WHITE, F_SM);
}

void updTouch(TouchPt t) {
  if (topBackHit(t)) { go(&SCR_MORE); return; }
  if (t.y >= SCR_H - 34) { otaInstall(); return; }
  if (t.y >= SCR_H - 68 && t.y < SCR_H - 38) { otaCheck(); drawUpd(); return; }
}
