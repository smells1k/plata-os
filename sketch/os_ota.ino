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
    if (u.endsWith(".bin")) {
      /* сначала ищем маленький образ для OTA — полный в слот не влезет */
      if (u.indexOf("-app.bin") > 0) { ghUrl = u; break; }
      if (!ghUrl.length()) ghUrl = u;
    }
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
  if (len > 1400000) {                     /* больше раздела приложения */
    h.end();
    updMsg = "файл великоват: " + String(len / 1024) + " КБ";
    updCol = C_ERR;
    drawUpd();
    return false;
  }
  if (!Update.begin((size_t)len)) {
    h.end();
    updMsg = "не влезает в память";
    updCol = C_ERR;
    drawUpd();
    return false;
  }

  WiFiClient *st = h.getStreamPtr();
  uint8_t buf[1024];
  size_t got = 0;
  int lastPct = -1;
  uint32_t t0 = millis();

  while (got < (size_t)len) {
    size_t want = (size_t)len - got;
    if (want > sizeof(buf)) want = sizeof(buf);
    size_t n = st->read(buf, want);
    if (n == 0) {
      if (!h.connected() && st->available() == 0) break;
      if (millis() - t0 > 20000) break;      /* зависло — выходим */
      delay(2);
      continue;
    }
    t0 = millis();
    if (Update.write(buf, n) != n) break;
    got += n;
    int p = (int)((uint64_t)got * 100 / len);
    if (p != lastPct) {
      lastPct = p;
      updPct = p;
      updBar();
    }
  }
  h.end();

  if (got == (size_t)len && Update.end(false)) {
    updPct = 100;
    updBar();
    updMsg = "готово, перезапускаюсь";
    updCol = C_OK;
    drawUpd();
    delay(1200);
    ESP.restart();
  }

  Update.abort();
  updMsg = "оборвалось на " + String((int)(got * 100 / len)) + "%";
  updCol = C_ERR;
  updPct = -1;
  drawUpd();
  return false;
}

/* ── экран ── */
void drawUpd() {
  statusDraw();
  headline(10, 24, "ОБНОВЛЕНИЕ", C_WHITE, 20);

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
  else textCenter(238, wifiOk ? "плата сама скачает и поставит" : "нужен Wi-Fi",
                  F_SM, C_DIM);

  btnDraw(10, SCR_H - 96, SCR_W - 20, 28, "Проверить обновление", C_PANEL, C_TXT, F_SM);
  btnDraw(10, SCR_H - 64, SCR_W - 20, 28, "Установить с GitHub", C_ACC, C_WHITE, F_SM);
  btnDraw(10, SCR_H - 32, SCR_W - 20, 28, "НАЗАД", C_PANEL, C_TXT, F_SM);
}

void updTouch(TouchPt t) {
  if (t.y >= SCR_H - 32) { go(&SCR_MORE); return; }
  if (t.y >= SCR_H - 64 && t.y < SCR_H - 36) { otaInstall(); return; }
  if (t.y >= SCR_H - 96 && t.y < SCR_H - 68) { otaCheck(); drawUpd(); return; }
}
