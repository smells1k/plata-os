/*
 *  ПЛАТА-ОС v7.0 — «операционка» для FireBeetle 2 ESP32-E
 *  2.8" TFT 240x320 (ILI9341) + тач (XPT2046) + WS2812 на D8
 *
 *  Экраны: стол · подсветка · часы+погода · компьютер · настройки · инфо · игры
 *  Wi-Fi настраивается с сенсора, кабель не нужен.
 *  Время по NTP · погода из интернета · ПК по Wi-Fi + Wake-on-LAN.
 */

#include <WiFi.h>
#include <math.h>
#if defined(ESP32)
  #include <WebServer.h>
  #include <Update.h>
  #define HAVE_OTA 1
#else
  #define HAVE_OTA 0
#endif
#include <ESPmDNS.h>
#include <Preferences.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <HTTPClient.h>
#include <time.h>
#include <WiFiUdp.h>

#include "ui_core.h"
#include "board_config.h"

/* Версия и канал управления. Меняется при каждой пересборке —
   по ней сразу видно, прошита ли новая прошивка. */
static const char *OS_VER = "14.0";
#if USE_USB_DISPLAY
static const char *OS_CH = "USB";
#else
static const char *OS_CH = "TFT";
#endif
Preferences cfg;

/* ══════════ ПИНЫ ══════════ */
#define PIN_TFT_CS    15
#define PIN_TFT_DC    2
#define PIN_TFT_RST   4
#define PIN_BL        32
#define PIN_TOUCH_CS  33
#define PIN_TOUCH_IRQ 36
#define RGB_PIN       5
#define BTN_PIN       27

XPT2046_Touchscreen ts(PIN_TOUCH_CS, PIN_TOUCH_IRQ);

int T_RAW_X0 = 300, T_RAW_X1 = 3800, T_RAW_Y0 = 300, T_RAW_Y1 = 3800;

bool touchRawRead(int16_t &rx, int16_t &ry) {
  if (!ts.touched()) return false;
  TS_Point p = ts.getPoint();
  rx = p.x; ry = p.y;
  return true;
}

/* ══════════ СОСТОЯНИЕ ══════════ */
String   wSsid, wPass;
uint8_t  blPwm = 210;
uint8_t  ledR = 0, ledG = 40, ledB = 180, ledBri = 200;
int      ledMode = 0;
uint16_t ledSpeed = 60;
bool     ledLit = false;

String   pcIp = "192.168.3.70";
uint16_t pcPort = 8080;
String   pcMac = "";
String   city = "Хабаровск";

String   boardIp = "";
bool     wifiOk = false;
bool     shadeOpen = false;
int      lightBarY[3] = { 153, 185, 217 };   /* где ползунки R/G/B */
int      lightBarBri  = 262;                 /* где ползунок яркости */
int      shadeY[2] = { 0, 0 };     /* где нарисованы ползунки */
bool     updNew = false;           /* на GitHub лежит версия новее */
String   updTag = "";              /* какая именно */
String   ghTag = "";               /* версия в последнем релизе GitHub */
String   ghUrl = "";               /* прямая ссылка на .bin */
uint32_t updLast = 0;              /* когда проверяли, millis() */
uint32_t wifiDown = 0;             /* с какого момента нет сети */
bool     otaOn    = false;         /* приёмник обновлений поднят */

int      rssiVal = 0;
String   weatherT = "—", weatherC = "";
uint32_t weatherAt = 0;


void navLauncher();          /* вернуться на рабочий стол */

/* ══════════ OTA: прошивка по воздуху ══════════ */
/* Принимаем тот же самый ПЛАТА-ОС.bin, что и кабелем:
   если файл 4 МБ — первые 0x10000 (загрузчик + разметка) пропускаем,
   в раздел приложения пишем только программу. */

static bool pcCmd(const char *cmd, String *out = nullptr);
static inline void pcDo(const char *cmd) { pcCmd(cmd, nullptr); }
void sendWol();

#if HAVE_OTA
static WebServer  otaSrv(80);
static uint32_t   otaSkip = 0;      /* сколько байт заголовка пропустить */
static uint32_t   otaSize = 0;      /* ожидаемый размер программы */
static uint32_t   otaGot  = 0;
static int        otaPct  = -1;

void otaScreen(const char *l1, const char *l2, int pct, uint16_t col) {
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, SCR_W, 3, col);
  textCenter(76, "ОБНОВЛЕНИЕ", F_LG, C_WHITE);
  textCenter(116, l1, F_MD, col);
  if (l2 && *l2) textCenter(140, l2, F_SM, C_DIM);
  if (pct >= 0) {
    int w = SCR_W - 60;
    tft.drawRect(30, 176, w, 24, C_LINE);
    if (pct > 0) tft.fillRect(32, 178, (w - 4) * pct / 100, 20, col);
    textCenter(212, (String(pct) + "%").c_str(), F_MD, C_TXT);
  }
  if (pct >= 0 && pct < 100) textDraw(16, SCR_H - 30, "не выключай плату", F_SM, C_DIM);
}

static String otaPage() {
  String h = "<!doctype html><meta charset=utf-8><title>ПЛАТА-ОС</title>";
  h += "<style>body{background:#0d1117;color:#e6edf3;font:16px sans-serif;";
  h += "text-align:center;padding:40px}input{margin:20px}</style>";
  h += "<h2>ПЛАТА-ОС " + String(OS_VER) + "</h2>";
  h += "<p>Сейчас прошито: " + String(OS_VER) + " · " + String(OS_CH) + "</p>";
  h += "<form method=POST action=/update enctype=multipart/form-data>";
  h += "<input type=file name=f><br><input type=submit value='Прошить'></form>";
  h += "<p style=color:#8b949e>выбери ПЛАТА-ОС.bin (тот же, что для кабеля)</p>";
  return h;
}

void otaStart() {
  static bool started = false;
  if (!wifiOk || started) return;
  started = true;
  otaOn = true;

  otaSrv.on("/", HTTP_GET, []() { otaSrv.send(200, "text/html; charset=utf-8", otaPage()); });

  otaSrv.on("/update", HTTP_POST,
    []() {                                   /* финал */
      bool ok = !Update.hasError();
      otaSrv.send(200, "text/html; charset=utf-8",
        ok ? "<meta charset=utf-8><body style='background:#0d1117;color:#3fb950;"
             "font:20px sans-serif;text-align:center;padding:60px'>"
             "Готово! Плата перезапускается…</body>"
           : "<meta charset=utf-8><body style='background:#0d1117;color:#f85149;"
             "font:20px sans-serif;text-align:center;padding:60px'>"
             "Не вышло. Попробуй ещё раз.</body>");
      if (ok) {
        otaScreen("готово", "перезапускаюсь…", 100, C_OK);
        delay(700);
        ESP.restart();
      } else {
        otaScreen("не получилось", "файл не тот?", -1, C_ERR);
        delay(2500);
        navLauncher();
      }
    },
    []() {                                   /* приём кусками */
      HTTPUpload &up = otaSrv.upload();
      if (up.status == UPLOAD_FILE_START) {
        otaGot = 0;
        otaPct = -1;
        /* 4 МБ = полный образ с загрузчиком → пропускаем 0x10000 */
        otaSkip = (up.totalSize > 0x180000UL) ? 0x10000UL : 0;
        otaSize = (up.totalSize > otaSkip) ? up.totalSize - otaSkip : 0;
        if (!Update.begin(otaSize ? otaSize : UPDATE_SIZE_UNKNOWN)) {
          otaScreen("нет места", "образ не влезает", -1, C_ERR);
        } else {
          otaScreen("приём прошивки", "по Wi-Fi", 0, C_OK);
        }
      } else if (up.status == UPLOAD_FILE_WRITE) {
        uint32_t off = 0;
        if (otaGot < otaSkip) {              /* заголовок выбрасываем */
          uint32_t drop = min((uint32_t)up.currentSize, otaSkip - otaGot);
          otaGot += drop;
          off = drop;
          if (drop >= up.currentSize) return;
        }
        Update.write(up.buf + off, up.currentSize - off);
        otaGot += up.currentSize - off;
        int p = otaSize ? (int)((uint64_t)(otaGot - otaSkip) * 100 / otaSize) : 0;
        if (p != otaPct) { otaPct = p; otaScreen("приём прошивки", "по Wi-Fi", p, C_OK); }
      } else if (up.status == UPLOAD_FILE_END) {
        Update.end(false);
      }
    });

  /* ── пульт для телефона: открывается страница самой платы ── */
  otaSrv.on("/pult", HTTP_GET, []() {
    String h = "<!doctype html><meta charset=utf-8>";
    h += "<meta name=viewport content='width=device-width,initial-scale=1'>";
    h += "<title>ПЛАТА-ОС</title>";
    h += "<style>body{margin:0;background:#0d1117;color:#e6edf3;"
         "font:15px system-ui,sans-serif;padding:14px;max-width:520px;margin:0 auto}"
         "h1{font-size:17px;margin:0 0 4px}.v{color:#8b949e;font-size:12px;margin-bottom:14px}"
         ".g{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:8px}"
         "a{display:block;padding:15px;border-radius:10px;background:#161b22;"
         "border:1px solid #30363d;color:#e6edf3;text-decoration:none;"
         "text-align:center;font-weight:600;font-size:13px}"
         "a:active{background:#30363d}.r{background:#2d1215;border-color:#8b2c33;color:#ffb3b8}"
         "</style>";
    h += "<h1>ПЛАТА-ОС</h1><div class=v>" + String(OS_VER) + " · " +
         (wifiOk ? boardIp : String("нет сети")) + "</div>";
    h += "<div class=g><a href='/p/on'>Разбудить ПК</a><a href='/p/lock'>Блокировка</a>"
         "<a href='/p/sleep'>Сон</a><a href='/p/shutdown' class=r>Выключить</a>"
         "<a href='/p/reboot' class=r>Перезагрузить</a><a href='/p/mute'>Без звука</a>"
         "<a href='/p/vol_down'>Тише</a><a href='/p/vol_up'>Громче</a></div>";
    h += "<div class=g><a href='/p/led'>Диод вкл/выкл</a><a href='/p/screen'>Экран вкл/выкл</a></div>";
    h += "<p style='color:#8b949e;font-size:12px'>Домашняя сеть. Ничего наружу не открыто.</p>";
    otaSrv.send(200, "text/html; charset=utf-8", h);
  });

  otaSrv.on("/p/on", HTTP_GET, []() { sendWol(); otaSrv.send(200, "text/plain; charset=utf-8", "пакет ушёл"); });
  otaSrv.on("/p/lock", HTTP_GET, []() { pcDo("lock"); otaSrv.send(200, "text/plain; charset=utf-8", "блокирую"); });
  otaSrv.on("/p/sleep", HTTP_GET, []() { pcDo("sleep"); otaSrv.send(200, "text/plain; charset=utf-8", "в сон"); });
  otaSrv.on("/p/shutdown", HTTP_GET, []() { pcDo("shutdown"); otaSrv.send(200, "text/plain; charset=utf-8", "выключаю"); });
  otaSrv.on("/p/reboot", HTTP_GET, []() { pcDo("reboot"); otaSrv.send(200, "text/plain; charset=utf-8", "перезагружаю"); });
  otaSrv.on("/p/mute", HTTP_GET, []() { pcDo("mute"); otaSrv.send(200, "text/plain; charset=utf-8", "звук"); });
  otaSrv.on("/p/vol_up", HTTP_GET, []() { pcDo("vol_up"); otaSrv.send(200, "text/plain; charset=utf-8", "громче"); });
  otaSrv.on("/p/vol_down", HTTP_GET, []() { pcDo("vol_down"); otaSrv.send(200, "text/plain; charset=utf-8", "тише"); });
  otaSrv.on("/p/led", HTTP_GET, []() {
    ledLit = !ledLit;
    if (ledLit) ledApply(); else neopixelWrite(RGB_PIN, 0, 0, 0);
    cfgSave();
    otaSrv.send(200, "text/plain; charset=utf-8", ledLit ? "диод включён" : "диод выключен");
  });
  otaSrv.on("/p/screen", HTTP_GET, []() {
    static bool off = false;
    off = !off;
    if (off) { blSet(1); } else { blSet(blPwm); }
    otaSrv.send(200, "text/plain; charset=utf-8", off ? "экран погашен" : "экран включён");
  });

  otaSrv.begin();
}
#endif

void otaTick() {
#if HAVE_OTA
  otaSrv.handleClient();
#endif
}

/* ══════════ ПОДСВЕТКА ══════════ */
static void blSet(uint8_t v) { blPwm = v; ledcWrite(PIN_BL, v); }
static uint8_t scl(uint8_t v) { return (uint16_t)v * ledBri / 255; }
static void showColor(uint8_t r, uint8_t g, uint8_t b) { neopixelWrite(RGB_PIN, scl(r), scl(g), scl(b)); }

static void hsv(uint16_t h, uint8_t s, uint8_t v, uint8_t &r, uint8_t &g, uint8_t &b) {
  uint8_t reg = h / 256, rem = (h % 256) * s / 255;
  uint8_t p = v - (uint16_t)v * s / 255, q = v - (uint16_t)v * rem / 255;
  uint8_t t = v - (uint16_t)v * (s - rem) / 255;
  switch (reg) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
}

static uint32_t fxStep = 0, lastFx = 0;

static void ledTick() {
  if (!ledLit || ledMode == 0) return;
  if (millis() - lastFx < ledSpeed) return;
  lastFx = millis(); fxStep++;
  uint8_t r, g, b;
  switch (ledMode) {
    case 1: hsv((fxStep * 8) % 1536, 255, 200, r, g, b); showColor(r, g, b); break;
    case 2: { float ph = (sin(fxStep * 0.06) + 1) * 0.5; uint8_t v = ph * 255;
      neopixelWrite(RGB_PIN, (uint16_t)scl(ledR) * v / 255, (uint16_t)scl(ledG) * v / 255, (uint16_t)scl(ledB) * v / 255); } break;
    case 3: neopixelWrite(RGB_PIN, (fxStep & 1) ? scl(ledR) : 0, (fxStep & 1) ? scl(ledG) : 0, (fxStep & 1) ? scl(ledB) : 0); break;
    case 4: if (fxStep & 1) neopixelWrite(RGB_PIN, scl(255), 0, 0); else neopixelWrite(RGB_PIN, 0, 0, scl(255)); break;
    case 5: { uint16_t f = 170 + esp_random() % 86;
      neopixelWrite(RGB_PIN, (uint16_t)scl(255) * f / 255, (uint16_t)scl(140) * f / 255, (uint16_t)scl(30) * f / 255); } break;
    case 6: hsv((fxStep * 3) % 1536, 255, 255, r, g, b); showColor(r, g, b); break;
    case 9: { uint32_t t = millis() % 1400; bool on = (t < 110) || (t > 220 && t < 330);
      neopixelWrite(RGB_PIN, on ? scl(255) : 0, 0, on ? scl(30) : 0); } break;
    case 10: if (esp_random() % 100 < 35) { hsv(esp_random() % 1536, 120, 220, r, g, b); showColor(r, g, b); }
             else neopixelWrite(RGB_PIN, 0, 0, 0); break;
    case 11: { float ph = (sin(fxStep * 0.04) + 1) * 0.5; uint8_t v = 20 + ph * 235;
      neopixelWrite(RGB_PIN, (uint16_t)scl(ledR) * v / 255, (uint16_t)scl(ledG) * v / 255, (uint16_t)scl(ledB) * v / 255); } break;
    default: showColor(ledR, ledG, ledB); break;
  }
}

static void ledApply() {
  ledLit = !(ledR == 0 && ledG == 0 && ledB == 0 && ledMode == 0);
  if (!ledLit) neopixelWrite(RGB_PIN, 0, 0, 0);
  else if (ledMode == 0) showColor(ledR, ledG, ledB);
}

/* ══════════ ПАМЯТЬ ══════════ */
static void cfgSave() {
  cfg.begin("os", false);
  cfg.putString("ssid", wSsid); cfg.putString("pass", wPass);
  cfg.putUChar("bl", blPwm);
  cfg.putUChar("lr", ledR); cfg.putUChar("lg", ledG); cfg.putUChar("lb", ledB);
  cfg.putUChar("lbri", ledBri); cfg.putInt("lmode", ledMode); cfg.putUShort("lspd", ledSpeed);
  cfg.putString("pcip", pcIp); cfg.putUShort("pcport", pcPort); cfg.putString("pcmac", pcMac);
  cfg.putString("city", city);
  cfg.putInt("tx0", T_RAW_X0); cfg.putInt("tx1", T_RAW_X1);
  cfg.putInt("ty0", T_RAW_Y0); cfg.putInt("ty1", T_RAW_Y1);
  cfg.end();
}

static void cfgLoad() {
  cfg.begin("os", true);
  wSsid = cfg.getString("ssid", ""); wPass = cfg.getString("pass", "");
  blPwm = cfg.getUChar("bl", 210);
  ledR = cfg.getUChar("lr", 0); ledG = cfg.getUChar("lg", 40); ledB = cfg.getUChar("lb", 180);
  ledBri = cfg.getUChar("lbri", 200); ledMode = cfg.getInt("lmode", 0); ledSpeed = cfg.getUShort("lspd", 60);
  pcIp = cfg.getString("pcip", "192.168.3.70"); pcPort = cfg.getUShort("pcport", 8080);
  if (pcPort == 0) pcPort = 8080;
  pcMac = cfg.getString("pcmac", ""); city = cfg.getString("city", "Хабаровск");
  T_RAW_X0 = cfg.getInt("tx0", 300); T_RAW_X1 = cfg.getInt("tx1", 3800);
  T_RAW_Y0 = cfg.getInt("ty0", 300); T_RAW_Y1 = cfg.getInt("ty1", 3800);
  cfg.end();
}

/* ══════════ ЭКРАНЫ ══════════ */
void drawLauncher();  void launcherTouch(TouchPt);
void drawUpd();       void updTouch(TouchPt);
void drawShade();     void shadeTouch(TouchPt);
void drawCalibScreen(); void calTouch(TouchPt); void calibTick();
void drawNote();      void noteTouch(TouchPt);
void openShade();     void closeShade();
void swipeHint(int x, int y);   bool swipeDone();
bool otaCheck(bool quiet);  bool otaInstall();
void otaStart();      void otaTick();
void bootScreen();    void bootOut();
void drawLight();     void lightTouch(TouchPt);
void drawClock();     void clockTouch(TouchPt);
void drawPC();        void pcTouch(TouchPt);
void drawSet();       void setTouch(TouchPt);
void drawInfo();      void infoTouch(TouchPt);
void drawGames();     void gamesTouch(TouchPt);
void drawMore();      void moreTouch(TouchPt);
void drawTorch();     void torchTouch(TouchPt);
void drawSys();       void sysTouch(TouchPt);
void drawReboot();    void rebootTouch(TouchPt);
void drawMemory();    void memoryTouch(TouchPt);
void drawReact();     void reactTouch(TouchPt);
void drawDice();      void diceTouch(TouchPt);
void drawCoin();      void coinTouch(TouchPt);
void drawSimon();     void simonTouch(TouchPt);
void drawTicTac();    void ttTouch(TouchPt);
void drawGuess();     void guessTouch(TouchPt);
void ledTest();

Screen SCR_LAUNCH = { "стол",     drawLauncher, launcherTouch };
Screen SCR_LIGHT  = { "подсвет", drawLight,    lightTouch };
Screen SCR_CLOCK  = { "часы",    drawClock,    clockTouch };
Screen SCR_PC     = { "ПК",      drawPC,       pcTouch };
Screen SCR_SET    = { "настр",   drawSet,      setTouch };
Screen SCR_INFO   = { "инфо",    drawInfo,     infoTouch };
Screen SCR_GAMES  = { "игры",    drawGames,    gamesTouch };
Screen SCR_MORE   = { "доп",     drawMore,     moreTouch };
Screen SCR_UPD    = { "обновл",  drawUpd,      updTouch };
Screen SCR_CAL    = { "калибр",  drawCalibScreen, calTouch };
Screen SCR_NOTE   = { "заметка", drawNote,     noteTouch };
Screen SCR_SHADE  = { "шторка",  drawShade,    shadeTouch };
Screen SCR_TORCH  = { "фонарь",  drawTorch,    torchTouch };
Screen SCR_SYS    = { "система", drawSys,      sysTouch };
Screen SCR_REBOOT = { "ребут",   drawReboot,   rebootTouch };
Screen SCR_MEMORY = { "память",  drawMemory,   memoryTouch };
Screen SCR_REACT  = { "реакция", drawReact,    reactTouch };
Screen SCR_DICE   = { "кубик",   drawDice,     diceTouch };
Screen SCR_COIN   = { "монетка", drawCoin,     coinTouch };
Screen SCR_SIMON  = { "саймон",  drawSimon,    simonTouch };
Screen SCR_TICTAC = { "крестики",drawTicTac,   ttTouch };
Screen SCR_GUESS  = { "угадай",  drawGuess,    guessTouch };

void wifiDialog();
void wifiConnect(bool showProgress);
void calibrate();
void sendWol();
void pcCmdUsb(const char *cmd);

static const Screen *curScreen = nullptr;

void navLauncher() { go(&SCR_LAUNCH); }

void go(const Screen *s) {
  curScreen = s;
  tft.fillScreen(C_BG);    /* fillScreen сам открывает новый кадр */
  if (s && s->draw) s->draw();
  statusDraw();            /* время и сеть — всегда сверху, на любом экране */
}

/* ══════════ ВРЕМЯ ══════════ */
/* Два источника: NTP (если есть Wi-Fi) и компьютер по USB (всегда работает). */
static bool     pcTimeOk = false;
static uint32_t pcEpoch = 0, pcAt = 0;
static int16_t  pcTz = 600;      /* Хабаровск +10 ч */

void pcTimeAccept(uint32_t epoch, int16_t tzMin) {
  pcEpoch = epoch;
  pcTz = tzMin;
  pcAt = millis();
  pcTimeOk = true;
}

static bool nowTime(struct tm &out) {
  struct tm t;
  if (getLocalTime(&t, 0)) { out = t; return true; }
  if (!pcTimeOk) return false;
  time_t secs = (time_t)(pcEpoch + (millis() - pcAt) / 1000 + (int32_t)pcTz * 60);
  gmtime_r(&secs, &t);
  out = t;
  return true;
}

static String timeStr(bool sec = false) {
  struct tm t;
  if (!nowTime(t)) return "--:--";
  char b[12];
  strftime(b, sizeof(b), sec ? "%H:%M:%S" : "%H:%M", &t);
  return String(b);
}

static void statusDraw() {
  tft.fillRect(0, 0, SCR_W, STATUS_H, C_PANEL);
  tft.drawFastHLine(0, STATUS_H, SCR_W, C_LINE);

  /* слева — время */
  textDraw(6, 5, timeStr().c_str(), F_SM, wifiOk ? C_TXT : C_DIM);

  /* справа — точки Wi-Fi */
  if (wifiOk) {
    int bars = rssiVal > -60 ? 4 : rssiVal > -72 ? 3 : 2;
    int wx = SCR_W - 58, wy = 15;
    for (int i = 0; i < 4; i++) {
      int h = 3 + i * 2;
      tft.fillRect(wx + i * 4, wy - h, 3, h, i < bars ? C_OK : C_LINE);
    }
  }

  /* всегда на месте: кнопка быстрых настроек */
  int hx = SCR_W - 34, hy = 3;
  tft.fillRoundRect(hx, hy, 30, 16, 4, shadeOpen ? C_ACC : C_PANEL2);
  for (int i = 0; i < 3; i++)
    tft.fillRect(hx + 5 + i * 7, hy + 7, 4, 2, shadeOpen ? C_BG : C_TXT);
}

/* заголовок с кнопкой «назад» сверху — на всех внутренних экранах */
static void topBack(const char *title, uint8_t px = 16) {
  statusDraw();
  btnDraw(4, 24, 46, 24, "<", C_PANEL2, C_WHITE, F_MD);
  headline(45, 26, title, C_WHITE, px);
}

/* ручка быстрых настроек в правом верхнем углу строки состояния */
static bool shadeHandleHit(TouchPt t) {
  return t.y <= STATUS_H + 6 && t.x >= SCR_W - 40;
}

static bool topBackHit(TouchPt t) {
  return t.y >= 22 && t.y <= 50 && t.x >= 2 && t.x <= 52;
}

static void drawNavBack(const char *label = "НАЗАД", int y = SCR_H - 40) {
  btnDraw(8, y, SCR_W - 16, 32, label, C_PANEL, C_TXT, F_SM);
}

/* ══════════ РАБОЧИЙ СТОЛ ══════════ */
/* 4×2 плитки: 4 столбца × 2 ряда. Высота = 64 + 2·(62+10) + 62 = 270 */
static const int TL_W = 56, TL_H = 62, TL_GX = 4, TL_GY = 10, TL_X0 = 4, TL_Y0 = 62;

/* иконки рисуются примитивами — без картинок во флеше */
static void iconDraw(int kind, int cx, int cy, uint16_t col) {
  switch (kind) {
    case 0:                              /* подсветка: лампа */
      tft.drawCircle(cx, cy - 2, 8, col);
      tft.fillRect(cx - 4, cy + 6, 8, 3, col);
      tft.fillRect(cx - 2, cy + 10, 4, 2, col);
      break;
    case 1:                              /* часы */
      tft.drawCircle(cx, cy, 10, col);
      tft.drawFastVLine(cx, cy - 6, 7, col);
      tft.drawFastHLine(cx, cy, 6, col);
      break;
    case 2:                              /* компьютер */
      tft.drawRect(cx - 11, cy - 9, 22, 14, col);
      tft.fillRect(cx - 5, cy + 7, 10, 2, col);
      tft.fillRect(cx - 8, cy + 10, 16, 2, col);
      break;
    case 3:                              /* игры: джойстик */
      tft.drawRoundRect(cx - 11, cy - 6, 22, 13, 5, col);
      tft.fillRect(cx - 7, cy - 1, 5, 2, col);
      tft.fillRect(cx - 6, cy - 3, 3, 6, col);
      tft.fillCircle(cx + 6, cy - 1, 2, col);
      tft.fillCircle(cx + 2, cy + 3, 2, col);
      break;
    case 4:                              /* настройки: шестерёнка */
      tft.drawCircle(cx, cy, 9, col);
      tft.fillCircle(cx, cy, 3, col);
      for (int a = 0; a < 8; a++) {
        int ax = cx + (int)(cos(a * 0.785) * 11);
        int ay = cy + (int)(sin(a * 0.785) * 11);
        tft.fillRect(ax - 1, ay - 1, 3, 3, col);
      }
      break;
    case 5:                              /* инфо */
      tft.drawCircle(cx, cy, 10, col);
      tft.fillRect(cx - 1, cy - 2, 3, 8, col);
      tft.fillRect(cx - 1, cy - 7, 3, 3, col);
      break;
    case 6:                              /* доп: плюс в рамке */
      tft.drawRoundRect(cx - 10, cy - 10, 20, 20, 4, col);
      tft.fillRect(cx - 7, cy - 1, 14, 3, col);
      tft.fillRect(cx - 1, cy - 7, 3, 14, col);
      break;
    case 7:                              /* заметка: листок */
      tft.drawRoundRect(cx - 9, cy - 11, 18, 22, 3, col);
      for (int i = 0; i < 3; i++)
        tft.fillRect(cx - 5, cy - 5 + i * 6, 10, 2, col);
      break;
  }
}

void drawLauncher() {
  statusDraw();

#if USE_USB_DISPLAY
  tft.bigText(8, 24, C_WHITE, 20, "ПЛАТА-ОС");
  tft.bigText(SCR_W - 8, 30, C_DIM, 9, OS_VER, 1);
#else
  textDraw(8, 26, "ПЛАТА-ОС", F_MD, C_WHITE);
  textRight(SCR_W - 8, 26, OS_VER, F_SM, C_DIM);
#endif

  const char *names[8] = { "Свет", "Часы", "ПК", "Игры", "Настр.", "Инфо", "Доп.", "Заметка" };
  uint16_t acc[8] = { 0xFBE0, 0x2D7F, 0x3666, 0xC986, 0x6B4D, 0x8010, 0x4A69, 0xFBE0 };

  for (int i = 0; i < 8; i++) {
    int col = i % 4, row = i / 4;
    int cx = TL_X0 + col * (TL_W + TL_GX);
    int cy = TL_Y0 + row * (TL_H + TL_GY);
    box(cx, cy, TL_W, TL_H, C_PANEL, C_LINE, 8);
    tft.fillRoundRect(cx + 1, cy + 1, TL_W - 2, 3, 1, acc[i]);
    iconDraw(i, cx + TL_W / 2, cy + 26, acc[i]);
    /* подпись мелким кеглем, по центру */
    int tw = textW(names[i], F_SM);
    textDraw(cx + (TL_W - tw) / 2, cy + TL_H - 17, names[i], F_SM, C_TXT);
  }

  /* полоска под плитками: цвет диода прямо сейчас */
  {
    int y = TL_Y0 + 2 * (TL_H + TL_GY) - TL_GY + 4;
    uint16_t cur = ledLit ? rgb565(ledR, ledG, ledB) : C_PANEL;
    tft.fillRoundRect(8, y, 14, 14, 3, cur);
    tft.drawRoundRect(8, y, 14, 14, 3, C_LINE);
    textDraw(28, y + 1, ledLit ? (String("диод ") + ledR + "," + ledG + "," + ledB).c_str()
                               : "диод выключен", F_SM, C_DIM);
  }

  /* нижняя подсказка */
  if (updNew && updTag.length())
    textCenter(SCR_H - 16, ("↑ есть " + updTag + " · Дополнительно").c_str(), F_SM, C_OK);
  else
    textDraw(8, SCR_H - 16, "нажми плитку", F_SM, C_DIM);
}

void launcherTouch(TouchPt t) {
  if (t.y < TL_Y0) return;
  int col = (t.x - TL_X0) / (TL_W + TL_GX);
  int row = (t.y - TL_Y0) / (TL_H + TL_GY);
  if (col < 0 || col > 3 || row < 0 || row > 1) return;
  if (t.x - TL_X0 - col * (TL_W + TL_GX) > TL_W) return;
  if (t.y - TL_Y0 - row * (TL_H + TL_GY) > TL_H) return;
  const Screen *all[8] = { &SCR_LIGHT, &SCR_CLOCK, &SCR_PC, &SCR_GAMES,
                           &SCR_SET, &SCR_INFO, &SCR_MORE, &SCR_NOTE };
  go(all[row * 4 + col]);
}


/* ══════════ ПОДСВЕТКА ══════════ */

void drawLight() {
  topBack("ПОДСВЕТКА", 18);

  /* ── текущий цвет ── */
  uint16_t cur = rgb565(ledR, ledG, ledB);
  tft.fillRoundRect(10, 48, 84, 84, 10, cur);
  tft.drawRoundRect(10, 48, 84, 84, 10, C_LINE);

  textDraw(104, 52, ledLit ? "включён" : "выключен", F_SM, ledLit ? C_OK : C_DIM);
  textDraw(104, 70, ("яркость " + String(ledBri)).c_str(), F_SM, C_DIM);
  textDraw(104, 88, ("R " + String(ledR)).c_str(), F_SM, C_ERR);
  textDraw(104, 104, ("G " + String(ledG)).c_str(), F_SM, C_OK);
  textDraw(104, 120, ("B " + String(ledB)).c_str(), F_SM, C_ACC);

  /* ── три ползунка: полоса в цвете самого канала ── */
  const char *nm[3] = { "К", "З", "С" };
  uint8_t val[3] = { ledR, ledG, ledB };
  for (int i = 0; i < 3; i++) {
    int y = 138 + i * 32;
    /* цвет канала — без примесей, чтобы не путаться */
    uint16_t chCol = (i == 0) ? rgb565(val[i], 0, 0)
                   : (i == 1) ? rgb565(0, val[i], 0)
                              : rgb565(0, 0, val[i]);
    textDraw(10, y, nm[i], F_SM, (i == 0) ? C_ERR : (i == 1) ? C_OK : C_ACC);
    textRight(SCR_W - 10, y, String(val[i]).c_str(), F_SM, C_TXT);
    barDraw(10, y + 15, SCR_W - 20, 12, val[i] * 100 / 255, chCol);
    lightBarY[i] = y + 15;
  }

  /* ── яркость самого диода ── */
  textDraw(10, 244, "ЯРКОСТЬ ДИОДА", F_SM, C_DIM);
  textRight(SCR_W - 10, 244, String(ledBri).c_str(), F_SM, C_TXT);
  barDraw(10, 262, SCR_W - 20, 14, (int)ledBri * 100 / 255, C_WARN);
  lightBarBri = 262;

  /* ── эффекты ── */
  const char *fx[4] = { "Радуга", "Дыхан.", "Строб", "Свеча" };
  for (int i = 0; i < 4; i++)
    btnDraw(10 + i * 56, 284, 52, 24, fx[i],
            ledMode == i + 1 ? C_ACC : C_PANEL, C_TXT, F_SM);

  btnDraw(10, SCR_H - 26, SCR_W - 20, 24, ledLit ? "ВЫКЛЮЧИТЬ" : "ВКЛЮЧИТЬ",
          ledLit ? C_ERR : C_OK, C_WHITE, F_SM);
}

void lightTouch(TouchPt t) {
  if (topBackHit(t)) { go(&SCR_LAUNCH); return; }

  /* включить/выключить */
  if (t.y >= SCR_H - 26) {
    ledLit = !ledLit;
    if (!ledLit) { ledMode = 0; neopixelWrite(RGB_PIN, 0, 0, 0); }
    else ledApply();
    cfgSave();
    go(&SCR_LIGHT);
    return;
  }

  /* ползунки RGB */
  for (int i = 0; i < 3; i++) {
    if (t.y >= lightBarY[i] && t.y < lightBarY[i] + 18) {
      int v = constrain(map(t.x, 10, SCR_W - 10, 0, 255), 0, 255);
      if (i == 0) ledR = v; else if (i == 1) ledG = v; else ledB = v;
      ledMode = 0;
      ledLit = true;
      ledApply();
      cfgSave();
      go(&SCR_LIGHT);
      return;
    }
  }

  /* яркость диода */
  if (t.y >= lightBarBri - 4 && t.y < lightBarBri + 18) {
    ledBri = constrain(map(t.x, 10, SCR_W - 10, 1, 255), 1, 255);
    ledLit = true;
    ledApply();
    cfgSave();
    go(&SCR_LIGHT);
    return;
  }

  /* эффекты */
  if (t.y >= 284 && t.y < 310) {
    int i = (t.x - 10) / 56;
    if (i < 0 || i > 3) return;
    ledMode = (ledMode == i + 1) ? 0 : i + 1;
    ledLit = true;
    if (ledMode) ledApply(); else ledApply();
    cfgSave();
    go(&SCR_LIGHT);
    return;
  }
}

/* ══════════ ЧАСЫ + ПОГОДА ══════════ */
static void weatherFetch();

void drawClock() {
  statusDraw();
  btnDraw(4, 24, 46, 24, "<", C_PANEL2, C_WHITE, F_MD);
  struct tm t;
  bool ok = nowTime(t);
  char hh[4] = "--", mm[4] = "--";
  if (ok) { strftime(hh, sizeof(hh), "%H", &t); strftime(mm, sizeof(mm), "%M", &t); }

  tft.setTextPadding(0);
  /* большие цифры: рисуем размером lg увеличенно */
  String big = String(hh) + ":" + String(mm);
#if USE_USB_DISPLAY
  {
    int approx = textW(big.c_str(), F_LG) * 56 / 22;
    tft.bigText((SCR_W - approx) / 2, 60, C_WHITE, 56, big.c_str());
  }
#else
  int w = textW(big.c_str(), F_LG);
  int x = (SCR_W - w) / 2;
  textDraw(x, 70, big.c_str(), F_LG, C_WHITE);
  textDraw(x + 1, 70, big.c_str(), F_LG, C_WHITE);
  textDraw(x, 71, big.c_str(), F_LG, C_WHITE);
#endif

  if (ok) {
    static const char *MON[12] = { "января","февраля","марта","апреля","мая","июня",
                                   "июля","августа","сентября","октября","ноября","декабря" };
    static const char *DAY[7] = { "воскресенье","понедельник","вторник","среда","четверг","пятница","суббота" };
    String d = String(t.tm_mday) + " " + MON[t.tm_mon];
    textCenter(112, d.c_str(), F_MD, C_TXT);
    textCenter(138, DAY[t.tm_wday], F_SM, C_DIM);
  }

  box(14, 170, SCR_W - 28, 92, C_PANEL, C_LINE, 10);
  textDraw(26, 182, "ПОГОДА", F_SM, C_DIM);
  textDraw(26, 200, weatherT.c_str(), F_LG, C_WHITE);
  if (weatherC.length()) textDraw(26, 232, textClip(weatherC, SCR_W - 60, F_SM).c_str(), F_SM, C_DIM);
  if (!wifiOk) textDraw(26, 232, "нет сети", F_SM, C_ERR);

  btnDraw(10, SCR_H - 40, 106, 32, "ПОГОДА", C_PANEL, C_TXT, F_SM);
  btnDraw(124, SCR_H - 40, 106, 32, "НАЗАД", C_PANEL, C_TXT, F_SM);
}

void clockTouch(TouchPt t) {
  if (topBackHit(t)) { go(&SCR_LAUNCH); return; }
  if (t.y < SCR_H - 40) return;
  if (t.x < 120) { weatherFetch(); go(&SCR_CLOCK); }   /* левая — погода */
  else go(&SCR_LAUNCH);                                 /* правая — назад */
}

/* ══════════ КОМПЬЮТЕР ══════════ */
static String pcStatus = "не проверен";
static uint16_t pcStatusCol = C_DIM;

/* ── автопоиск компьютера ──
   Агент объявляет себя в сети как «pult-pc.local». Плата спрашивает у mDNS
   адрес службы _pult._tcp и подставляет IP + порт. Руками адрес вписывать
   не нужно — и нельзя ошибиться чужой сетью (172.16.x и т.п.). */
static String pcFoundName = "";
static uint32_t pcFindAt = 0;

static void pcAutoIp() {
  if (!wifiOk) {
    pcStatus = "сначала Wi-Fi";
    pcStatusCol = C_ERR;
    return;
  }
  if (pcPort == 0) pcPort = 8080;
  MDNS.begin("board-os");                 /* без этого запросы не уходят */
  delay(30);
  int n = MDNS.queryService("pult", "tcp");
  if (n > 0) {
    IPAddress a = MDNS.address(0);
    uint16_t prt = MDNS.port(0);
    pcIp = a.toString();
    if (prt) pcPort = prt;
    pcFoundName = MDNS.hostname(0);
    cfgSave();
    pcStatus = "найден: " + pcFoundName;
    pcStatusCol = C_OK;
    pcFindAt = millis();
    return;
  }
  pcStatus = "ПК не найден в сети";
  pcStatusCol = C_ERR;
}

/* одна ли сеть у платы и ПК — самая частая причина «не отвечает» */
static bool sameSubnet() {
  IPAddress a, b;
  if (!a.fromString(boardIp) || !b.fromString(pcIp)) return false;
  return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

static bool pcCmd(const char *cmd, String *out) {
#if USE_USB_DISPLAY
  /* по проводу — основной путь, Wi-Fi не нужен */
  pcCmdUsb(cmd);
  return true;
#else
  if (!wifiOk) return false;
  HTTPClient h;
  h.setConnectTimeout(1200);
  h.setTimeout(1500);
  String url = "http://" + pcIp + ":" + String(pcPort) + "/" + cmd;
  if (!h.begin(url)) return false;
  int code = h.GET();
  if (code == 200 && out) *out = h.getString();
  h.end();
  return code == 200;
#endif
}

/* Самопроверка: плата отправляет агенту свой адрес, он отвечает,
   в одной ли вы сети. Сразу видно, кто виноват — сеть или агент. */
void pcDebug() {
#if USE_USB_DISPLAY
  /* по проводу: просто отправляем проверку, окно на ПК её выполнит */
  pcCmdUsb("ping");
  pcStatus = "проверка по проводу";
  pcStatusCol = C_OK;
#else
  if (!wifiOk) {
    pcStatus = "нет Wi-Fi";
    pcStatusCol = C_ERR;
    return;
  }
  HTTPClient h;
  h.setConnectTimeout(2000);
  h.setTimeout(2500);
  String url = "http://" + pcIp + ":" + String(pcPort) + "/debug?from=" + boardIp;
  if (!h.begin(url)) {
    pcStatus = "агент не отвечает";
    pcStatusCol = C_ERR;
    return;
  }
  int code = h.GET();
  if (code == 200) {
    String b = h.getString();
    if (b.indexOf("\"same_subnet\":\"да\"") > 0) {
      pcStatus = "сеть одна, всё ок";
      pcStatusCol = C_OK;
    } else if (b.indexOf("НЕТ") > 0) {
      pcStatus = "РАЗНЫЕ СЕТИ";
      pcStatusCol = C_ERR;
    } else {
      pcStatus = "агент ответил";
      pcStatusCol = C_OK;
    }
  } else {
    pcStatus = "агент ответил код " + String(code);
    pcStatusCol = C_ERR;
  }
  h.end();
#endif
}

static void pcPing() {
#if USE_USB_DISPLAY
  pcStatus = "по проводу USB";
  pcStatusCol = C_OK;
  return;
#else
  if (!wifiOk) { pcStatus = "нет сети"; pcStatusCol = C_ERR; return; }
  if (pcCmd("ping")) { pcStatus = "на связи"; pcStatusCol = C_OK; }
  else if (!sameSubnet() && pcIp.length()) { pcStatus = "разные сети"; pcStatusCol = C_ERR; }
  else { pcStatus = "нет ответа · firewall?"; pcStatusCol = C_ERR; }
#endif
}

/* ══════════ КОМПЬЮТЕР ══════════ */
/* Пульт ПК. Всё по кабелю — статус всегда живой. */
int    pcVol = 50;          /* громкость, приходит от ПК */
bool   pcMuted = false;
String pcHost = "";         /* имя компьютера от ПК */

static void drawVolBar(int x, int y, int w, int h) {
  /* рамка + заполнение по громкости */
  tft.drawRect(x, y, w, h, C_LINE);
  int fill = (w - 4) * constrain(pcVol, 0, 100) / 100;
  if (fill > 0) tft.fillRect(x + 2, y + 2, fill, h - 4, pcMuted ? C_ERR : C_ACC);
  for (int i = 1; i < 10; i++) {          /* деления */
    int dx = x + 2 + (w - 4) * i / 10;
    tft.drawFastVLine(dx, y + 2, h - 4, C_BG);
  }
}

void drawPC() {
  topBack("КОМПЬЮТЕР", 15);

  /* ── строка связи ── */
  box(8, 50, SCR_W - 16, 28, C_PANEL, C_LINE, 6);
  tft.fillCircle(18, 64, 4, C_OK);
#if USE_USB_DISPLAY
  textDraw(28, 56, (pcHost.length() ? "по проводу · " + pcHost : String("по проводу")).c_str(),
           F_SM, C_OK);
#else
  textDraw(28, 53, "сеть · " + pcIp, F_SM, pcStatusCol);
#endif

  /* ── громкость ── */
  textDraw(8, 84, "ГРОМКОСТЬ", F_SM, C_DIM);
  textRight(SCR_W - 8, 84, pcMuted ? "выкл" : (String(pcVol) + "%").c_str(), F_SM,
            pcMuted ? C_ERR : C_TXT);
  drawVolBar(8, 100, SCR_W - 16, 16);

  const char *bm[3] = { "−", "+", "Мут" };
  for (int i = 0; i < 3; i++)
    btnDraw(8 + i * 78, 122, 72, 30, bm[i],
            (i == 2 && pcMuted) ? C_ERR : C_PANEL, C_TXT, F_MD);

  /* ── мультимедиа ── */
  const char *bp[3] = { "◀◀", "▶‖", "▶▶" };
  for (int i = 0; i < 3; i++)
    btnDraw(8 + i * 78, 158, 72, 30, bp[i], C_PANEL, C_TXT, F_MD);

  /* ── питание ── */
  textDraw(8, 196, "ПИТАНИЕ", F_SM, C_DIM);
  btnDraw(8, 212, 108, 32, "Заблок.", C_PANEL, C_TXT, F_SM);
  btnDraw(124, 212, 108, 32, "Сон", C_PANEL, C_TXT, F_SM);
  btnDraw(8, 248, 108, 32, "Выключ.", C_ERR, C_WHITE, F_SM);
  btnDraw(124, 248, 108, 32, "Перезагр.", C_ERR, C_WHITE, F_SM);

  /* ── низ ── */
  btnDraw(8, SCR_H - 36, 108, 30, "⚡ Разбудить", C_PANEL, C_TXT, F_SM);
}

void pcTouch(TouchPt t) {
  if (topBackHit(t)) { go(&SCR_LAUNCH); return; }
  /* ── нижний ряд ── */
  if (t.y >= SCR_H - 36) {
    {                                      /* разбудить */
      if (!pcMac.length()) { pcStatus = "MAC не задан"; pcStatusCol = C_WARN; go(&SCR_PC); return; }
      sendWol();
      pcStatus = "WoL отправлен";
      pcStatusCol = C_OK;
      go(&SCR_PC);
      return;
    }
    go(&SCR_LAUNCH);
    return;
  }

  /* ── громкость: − + мут ── */
  if (t.y >= 122 && t.y < 152) {
    int i = (t.x - 8) / 78;
    if (i == 0) { pcVol = max(0, pcVol - 5);   pcDo("vol_down"); }
    else if (i == 1) { pcVol = min(100, pcVol + 5); pcDo("vol_up"); }
    else if (i == 2) { pcMuted = !pcMuted;      pcDo("mute"); }
    go(&SCR_PC);
    return;
  }

  /* ── мультимедиа: назад · пауза · вперёд ── */
  if (t.y >= 158 && t.y < 188) {
    int i = (t.x - 8) / 78;
    if (i == 0) pcCmd("prev");
    else if (i == 1) pcCmd("playpause");
    else if (i == 2) pcCmd("next");
    pcStatus = "команда ушла";
    pcStatusCol = C_OK;
    go(&SCR_PC);
    return;
  }

  /* ── питание ── */
  if (t.y >= 212 && t.y < 280) {
    int col = t.x < 116 ? 0 : 1;
    int row = t.y < 248 ? 0 : 1;
    const char *cmds[4] = { "lock", "sleep", "shutdown", "reboot" };
    pcCmd(cmds[row * 2 + col]);
    pcStatus = row ? "ухожу…" : "команда ушла";
    pcStatusCol = C_OK;
    if (row) { delay(350); }        /* дать окну выполнить до перерисовки */
    go(&SCR_PC);
    return;
  }

  /* ── строка связи: перепроверить ── */
  if (t.y >= 46 && t.y < 76) {
    pcCmdUsb("status");
    pcStatus = "проверяю…";
    pcStatusCol = C_OK;
    go(&SCR_PC);
    return;
  }
}

/* ══════════ WAKE-ON-LAN ══════════ */
static bool parseMac(const String &s, uint8_t mac[6]) {
  int v[6], n = 0;
  const char *p = s.c_str();
  while (*p && n < 6) {
    while (*p && !isxdigit(*p)) p++;
    if (!*p) break;
    v[n++] = strtol(p, (char **)&p, 16);
  }
  if (n < 6) return false;
  for (int i = 0; i < 6; i++) mac[i] = (uint8_t)v[i];
  return true;
}

void sendWol();

void sendWol() {
  /* ПК выключен — значит окна на нём нет, и по проводу просить некого.
     Пакет шлёт сама плата по Wi-Fi. Провод — только запасной путь. */
  uint8_t mac[6];
  if (!parseMac(pcMac, mac)) {
#if USE_USB_DISPLAY
    pcCmdUsb((String("wol ") + pcMac).c_str());
#endif
    return;
  }
  if (wifiOk) {
  uint8_t pkt[102];
  memset(pkt, 0xFF, 6);
  for (int i = 0; i < 16; i++) memcpy(pkt + 6 + i * 6, mac, 6);

  WiFiUDP udp;
  udp.begin(9);
  IPAddress bc = WiFi.localIP();
  bc[3] = 255;
  for (int k = 0; k < 3; k++) {
    udp.beginPacket(IPAddress(255, 255, 255, 255), 9); udp.write(pkt, 102); udp.endPacket();
    udp.beginPacket(bc, 9); udp.write(pkt, 102); udp.endPacket();
    delay(40);
  }
  udp.stop();
    return;
  }
#if USE_USB_DISPLAY
  pcCmdUsb((String("wol ") + pcMac).c_str());
#endif
}

/* ══════════ КЛАВИАТУРА ══════════ */
/* Раскладки — массивами codepoint'ов, чтобы кириллица не превращалась в мусор */
static const uint16_t KB_RU[3][11] = {
  { 0x439,0x446,0x443,0x43A,0x435,0x43D,0x433,0x448,0x449,0x437,0 },
  { 0x444,0x44B,0x432,0x430,0x43F,0x440,0x43E,0x43B,0x434,0x436,0x44D },
  { 0x44F,0x447,0x441,0x43C,0x438,0x442,0x44C,0x431,0x44E,0x451,0 },
};
static const uint16_t KB_EN[3][11] = {
  { 'q','w','e','r','t','y','u','i','o','p',0 },
  { 'a','s','d','f','g','h','j','k','l','-',0 },
  { 'z','x','c','v','b','n','m','.','_','@',0 },
};
static const uint16_t KB_NUM[3][11] = {
  { '1','2','3','4','5','6','7','8','9','0',0 },
  { '-',':','/','.',',','+','=','(',')','_',0 },
  { ' ','"','\'','[',']','{','}','#','%','*',0 },
};

#define UPPER(cp) ((cp) >= 0x430 && (cp) <= 0x44F ? (cp) - 0x20 : \
                   (cp) >= 'a' && (cp) <= 'z' ? (cp) - 32 : (cp))

/* общая клавиатура. true — ввод подтверждён */
static bool kbInput(const char *title, String &buf, bool numericOnly = false, bool secret = false) {
  int page = numericOnly ? 2 : 0;
  bool upper = false;
  bool full = true;        /* перерисовать всю клавиатуру (а не только строку ввода) */

  const int kx0 = 6, ky0 = 94, kw = 22, kh = 30, kg = 2;
  const int ry = ky0 + 3 * (kh + kg) + 4;
  const int ry2 = ry + 38;

  while (true) {
    const uint16_t (*lay)[11] = (page == 0) ? KB_RU : (page == 1) ? KB_EN : KB_NUM;

    if (full) {
      tft.fillScreen(C_BG);
      statusDraw();
      textDraw(10, 24, title, F_MD, C_WHITE);
      box(10, 50, SCR_W - 20, 34, C_PANEL2, C_ACC, 6);

      for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 10; c++) {
          uint16_t cp = lay[r][c];
          if (!cp) continue;
          if (upper && page != 2) cp = UPPER(cp);
          int x = kx0 + c * (kw + kg), y = ky0 + r * (kh + kg);
          box(x, y, kw, kh, C_PANEL, C_LINE, 5);
          glyphCenter(x + kw / 2, y + kh / 2, cp, F_MD, C_TXT);
        }
      }

      btnDraw(6, ry, 40, 32, "123", page == 2 ? C_ACC : C_PANEL, C_TXT, F_SM);
      btnDraw(50, ry, 40, 32, "абв", page == 0 ? C_ACC : C_PANEL, C_TXT, F_SM);
      btnDraw(94, ry, 40, 32, "ABC", page == 1 ? C_ACC : C_PANEL, C_TXT, F_SM);
      btnDraw(138, ry, 40, 32, upper ? "↑" : "↑↓", upper ? C_ACC : C_PANEL, C_TXT, F_SM);
      btnDraw(182, ry, 52, 32, "ВВОД", C_ACC, C_WHITE, F_SM);

      btnDraw(6, ry2, 116, 32, "ПРОБЕЛ", C_PANEL, C_TXT, F_SM);
      btnDraw(126, ry2, 56, 32, "←", C_PANEL, C_TXT, F_MD);
      btnDraw(186, ry2, 48, 32, "ОТМ", C_ERR, C_WHITE, F_SM);

      full = false;
    }

    /* строка ввода меняется на каждое нажатие — перерисовываем только её,
       иначе на каждую клавишу уходит целый кадр */
    tft.fillRect(11, 51, SCR_W - 22, 32, C_PANEL2);
    String shown = buf;
    if (secret) { shown = ""; for (uint16_t i = 0; i < buf.length(); i++) shown += '*'; }
    if (shown.length() > 20) shown = "…" + shown.substring(shown.length() - 19);
    textDraw(18, 58, shown.c_str(), F_MD, C_TXT);
    int cw = textW(shown.c_str(), F_MD);
    if ((millis() / 400) % 2 == 0) tft.fillRect(20 + cw, 56, 2, 22, C_ACC);

    TouchPt t = touchWait();
    if (!t.ok) continue;

    if (t.y >= ry2 && t.y < ry2 + 32) {
      if (t.x < 126) { buf += ' '; continue; }
      if (t.x < 182) { if (buf.length()) buf.remove(buf.length() - 1); continue; }
      return false;                                    /* ОТМ */
    }
    if (t.y >= ry && t.y < ry + 32) {
      if (t.x < 46) { if (page != 2) { page = 2; full = true; } continue; }
      if (t.x < 90) { if (page != 0) { page = 0; full = true; } continue; }
      if (t.x < 134) { if (page != 1) { page = 1; full = true; } continue; }
      if (t.x < 178) { upper = !upper; full = true; continue; }
      return true;                                     /* ВВОД */
    }
    if (t.y >= ky0 && t.y < ky0 + 3 * (kh + kg)) {
      int r = (t.y - ky0) / (kh + kg);
      int c = (t.x - kx0) / (kw + kg);
      if (r > 2 || c > 9 || c < 0) continue;
      uint16_t cp = lay[r][c];
      if (!cp) continue;
      if (upper && page != 2) cp = UPPER(cp);
      char b[4]; cpToUtf8(cp, b);
      if (buf.length() < 60) buf += b;
      continue;
    }
  }
}

/* ══════════ WI-FI ══════════ */
void wifiDialog() {
  tft.fillScreen(C_BG);
  statusDraw();
  textDraw(10, 28, "ПОИСК СЕТЕЙ", F_MD, C_WHITE);
  textCenter(70, "сканирую…", F_MD, C_DIM);
  WiFi.scanDelete();
  int n = WiFi.scanNetworks();
  if (n <= 0) {
    textCenter(140, "ничего не найдено", F_SM, C_ERR);
    delay(1200);
    go(&SCR_SET);
    return;
  }

  /* уникальные SSID, по силе сигнала */
  String ssid[12]; int rss[12]; int cnt = 0;
  for (int i = 0; i < n && cnt < 12; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;
    bool dup = false;
    for (int j = 0; j < cnt; j++) if (ssid[j] == s) dup = true;
    if (dup) continue;
    ssid[cnt] = s; rss[cnt] = WiFi.RSSI(i); cnt++;
  }

  tft.fillScreen(C_BG);
  statusDraw();
  textDraw(10, 28, "ВЫБЕРИ СЕТЬ", F_MD, C_WHITE);

  for (int i = 0; i < cnt && i < 8; i++) {
    int y = 56 + i * 30;
    box(8, y, SCR_W - 16, 26, C_PANEL, C_LINE, 5);
    textDraw(16, y + 5, textClip(ssid[i], 150, F_SM).c_str(), F_SM, C_TXT);
    int bars = rss[i] > -60 ? 4 : rss[i] > -72 ? 3 : 2;
    for (int b = 0; b < 4; b++)
      tft.fillRect(SCR_W - 34 + b * 5, y + 18 - (3 + b * 2), 4, 3 + b * 2, b < bars ? C_OK : C_LINE);
  }
  btnDraw(8, SCR_H - 38, SCR_W - 16, 32, "ОТМЕНА", C_PANEL, C_TXT, F_SM);

  TouchPt t = touchWait();
  if (!t.ok) { go(&SCR_SET); return; }
  if (t.y >= SCR_H - 38) { go(&SCR_SET); return; }
  int idx = (t.y - 56) / 30;
  if (t.y < 56 || idx < 0 || idx >= cnt || idx >= 8) { go(&SCR_SET); return; }

  String pass = wPass;
  if (!kbInput((String("ПАРОЛЬ: ") + textClip(ssid[idx], 90, F_SM)).c_str(), pass, false, true)) {
    go(&SCR_SET);
    return;
  }
  wSsid = ssid[idx];
  wPass = pass;
  cfgSave();
  wifiConnect(true);
  go(&SCR_SET);
}

void wifiConnect(bool showProgress) {
  if (!wSsid.length()) return;
  if (showProgress) {
    tft.fillScreen(C_BG);
    statusDraw();
    textCenter(120, "подключаюсь…", F_MD, C_TXT);
    textCenter(146, textClip(wSsid, SCR_W - 30, F_SM).c_str(), F_SM, C_DIM);
  }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);          /* сам вернётся, если сеть пропала */
  WiFi.begin(wSsid.c_str(), wPass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(250);
    if (showProgress) {
      int p = (millis() - t0) * 100 / 15000;
      barDraw(30, 180, SCR_W - 60, 14, p, C_ACC);
    }
  }
  wifiOk = (WiFi.status() == WL_CONNECTED);
  if (wifiOk) {
    boardIp = WiFi.localIP().toString();
    rssiVal = WiFi.RSSI();
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    setenv("TZ", "<+10>-10", 1);
    tzset();
    MDNS.begin("board-os");
    delay(30);
    pcAutoIp();                    /* сам найдёт агент в этой же сети */
    weatherFetch();
  } else {
    boardIp = "";
  }
  if (showProgress) {
    tft.fillScreen(C_BG);
    statusDraw();
    if (wifiOk) {
      textCenter(120, "ПОДКЛЮЧЕНО", F_MD, C_OK);
      textCenter(150, boardIp.c_str(), F_MD, C_TXT);
      textCenter(185, "открой этот адрес", F_SM, C_DIM);
      textCenter(205, "в браузере телефона", F_SM, C_DIM);
    } else {
      textCenter(120, "не вышло", F_MD, C_ERR);
      textCenter(150, "проверь пароль и 2.4 ГГц", F_SM, C_DIM);
    }
    delay(1800);
  }
}

/* ══════════ ПОГОДА ══════════ */
static void weatherFetch() {
  if (!wifiOk) return;
  HTTPClient h;
  h.setConnectTimeout(3000);
  h.setTimeout(4000);
  String url = "http://wttr.in/" + String(city) + "?format=%t|%C&lang=ru";
  if (!h.begin(url)) return;
  h.addHeader("User-Agent", "curl/8.0");
  int code = h.GET();
  if (code == 200) {
    String r = h.getString();
    r.trim();
    int bar = r.indexOf('|');
    if (bar > 0) { weatherT = r.substring(0, bar); weatherC = r.substring(bar + 1); }
    else weatherT = r;
    weatherT.trim(); weatherC.trim();
    weatherAt = millis();
  }
  h.end();
}

/* ══════════ ИНФО ══════════ */
void drawInfo() {
  topBack("О СИСТЕМЕ", 16);

  struct Row { const char *k; String v; };
  Row rows[9] = {
    { "Версия", OS_VER },
    { "Канал ПК", OS_CH },
    { "Чип", ESP.getChipModel() },
    { "Частота", String(getCpuFrequencyMhz()) + " МГц" },
    { "Память", String(ESP.getFreeHeap() / 1024) + " КБ" },
    { "Флеш", String(ESP.getFlashChipSize() / 1048576) + " МБ" },
    { "Темп.", String(temperatureRead(), 1) + " °C" },
    { "Сеть", wifiOk ? wSsid : "нет" },
    { "Адрес", wifiOk ? boardIp : "—" },
  };
  int y = 54;
  for (int i = 0; i < 9; i++) {
    if (i % 2 == 0) tft.fillRect(8, y - 2, SCR_W - 16, 26, C_PANEL);
    textDraw(14, y + 4, rows[i].k, F_SM, C_DIM);
    textRight(SCR_W - 14, y + 4, textClip(rows[i].v, 130, F_SM).c_str(), F_SM, C_TXT);
    y += 26;
  }
  btnDraw(10, SCR_H - 38, SCR_W - 20, 32, "НАЗАД", C_PANEL, C_TXT, F_SM);
}

void infoTouch(TouchPt t) {
  if (topBackHit(t)) { go(&SCR_LAUNCH); return; }
  if (t.y >= SCR_H - 38) go(&SCR_LAUNCH);
}

/* ══════════ КАЛИБРОВКА КАК ПРОГРАММА ══════════ */
/* Открывается экраном, а не «провалом». Всегда можно выйти:
   кнопка «‹» сверху, кнопка «ОТМЕНА» внизу и D4 в любой момент. */

static int  calStep = -1;           /* -1 — экран-приглашение, 0..3 — точки */
static int  calRawX[4], calRawY[4];
static bool calBusy = false;

void drawCalibScreen() {
  topBack("КАЛИБРОВКА", 16);

  if (calStep < 0) {
    box(10, 52, SCR_W - 20, 120, C_PANEL, C_LINE, 8);
    textDraw(20, 62, "Сенсор со временем", F_SM, C_TXT);
    textDraw(20, 80, "съезжает: палец жмёт", F_SM, C_TXT);
    textDraw(20, 98, "в одно, а срабатывает", F_SM, C_TXT);
    textDraw(20, 116, "в другом месте.", F_SM, C_TXT);
    textDraw(20, 144, "4 точки, по одной.", F_SM, C_DIM);

    btnDraw(10, 190, SCR_W - 20, 36, "НАЧАТЬ", C_ACC, C_WHITE, F_MD);
    btnDraw(10, 236, SCR_W - 20, 34, "ОТМЕНА", C_PANEL, C_TXT, F_SM);
    textDraw(10, SCR_H - 22, "или зажми D4 на 5 сек — выключение", F_SM, C_DIM);
    return;
  }

  /* точка */
  const int pts[4][2] = { {20, 46}, {SCR_W - 20, 46},
                          {20, SCR_H - 46}, {SCR_W - 20, SCR_H - 46} };
  int i = calStep;
  tft.drawCircle(pts[i][0], pts[i][1], 13, C_ACC);
  tft.drawCircle(pts[i][0], pts[i][1], 6, C_ACC);
  textCenter(SCR_H / 2, (String("точка ") + (i + 1) + " из 4").c_str(), F_MD, C_TXT);
  textCenter(SCR_H / 2 + 24, "нажми в центр кружка", F_SM, C_DIM);

  btnDraw(10, SCR_H - 34, SCR_W - 20, 30, "ПРЕРВАТЬ", C_PANEL, C_TXT, F_SM);
}

void calibrate() {
  calStep = -1;
  go(&SCR_CAL);
}

/* вызывается из loop: если в режиме калибровки — ждём касания */
void calibTick() {
  if (calStep < 0) return;
  int16_t rx, ry;
  if (!touchRawRead(rx, ry)) return;

  /* запомнили точку */
  calRawX[calStep] = rx;
  calRawY[calStep] = ry;
  calStep++;

  if (calStep > 3) {                 /* все четыре собраны */
    T_RAW_X0 = calRawX[0]; T_RAW_X1 = calRawX[1];
    T_RAW_Y0 = calRawY[0]; T_RAW_Y1 = calRawY[3];
    cfgSave();
    calStep = -1;
    go(&SCR_CAL);
    textCenter(SCR_H / 2, "ГОТОВО", F_MD, C_OK);
    delay(900);
    go(&SCR_SET);
    return;
  }
  go(&SCR_CAL);
}

void calTouch(TouchPt t) {
  if (topBackHit(t)) { calStep = -1; go(&SCR_SET); return; }
  if (calStep < 0) {
    if (t.y >= 190 && t.y < 226) { calStep = 0; go(&SCR_CAL); return; }
    if (t.y >= 236 && t.y < 270) { go(&SCR_SET); return; }
    return;
  }
  if (t.y >= SCR_H - 34) { calStep = -1; go(&SCR_SET); return; }   /* прервать */
}

/* ══════════ SETUP / LOOP ══════════ */
void setup() {
#if USE_USB_DISPLAY
  Serial.begin(460800);          /* экран по USB — нужен быстрый порт */
#else
  Serial.begin(115200);
#endif
  pinMode(BTN_PIN, INPUT_PULLUP);
  neopixelWrite(RGB_PIN, 0, 0, 0);

  cfgLoad();

  ledcAttach(PIN_BL, 5000, 8);
  blSet(blPwm);

#if !USE_USB_DISPLAY
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(C_BG);

  SPI.begin(18, 19, 23, PIN_TOUCH_CS);
  ts.begin();
  ts.setRotation(0);
#endif

#if USE_USB_DISPLAY
  delay(300);
  pcCmdUsb("status");        /* узнаём громкость и имя ПК сразу */
  pcCmdUsb("host");
#endif

  bootScreen();                 /* заставка с анимацией */
  delay(150);

  /* аварийный вход в калибровку: кнопка D4 зажата при включении */
#if !USE_USB_DISPLAY
  if (digitalRead(BTN_PIN) == LOW) {
    calibrate();
    go(&SCR_LAUNCH);
    return;
  }
#endif

  if (wSsid.length()) wifiConnect(false);
  else {
    tft.fillScreen(C_BG);
    statusDraw();
    textCenter(110, "Wi-Fi не настроен", F_MD, C_WARN);
    textCenter(140, "зайди в Настройки", F_SM, C_DIM);
    textCenter(160, "и выбери сеть", F_SM, C_DIM);
    delay(1600);
  }

#if HAVE_OTA
  otaStart();
#endif

  ledApply();
  go(&SCR_LAUNCH);
}

uint32_t lastUi = 0;

#if USE_USB_DISPLAY
/* команды от окна на компьютере */
#if USE_USB_DISPLAY
/* ── команды компьютеру по проводу ──
   Плата шлёт по USB: 'P' + длина + имя команды ("lock", "shutdown"…).
   Окно на ПК их выполняет — сеть не нужна вообще. */
void pcCmdUsb(const char *cmd) {
  size_t n = strlen(cmd);
  if (n > 32) n = 32;
  Serial.write('P');
  Serial.write((uint8_t)n);
  for (size_t i = 0; i < n; i++) Serial.write((uint8_t)cmd[i]);
}
#endif

static void usbCommands() {
  while (Serial.available()) {
    int c = Serial.read();
    if (c == 'S') {                        /* состояние ПК */
      uint8_t n = 0; uint32_t t0 = millis();
      while (!Serial.available() && millis() - t0 < 60) delay(1);
      if (!Serial.available()) continue;
      n = Serial.read();
      String st = "";
      uint32_t t1 = millis();
      while (st.length() < n && millis() - t1 < 150) {
        if (Serial.available()) st += (char)Serial.read();
      }
      /* разбор "vol=55 muted=0 host=name" */
      int iv = st.indexOf("vol=");
      if (iv >= 0) pcVol = constrain(st.substring(iv + 4, iv + 7).toInt(), 0, 100);
      int im = st.indexOf("muted=");
      if (im >= 0) pcMuted = st.charAt(im + 6) == '1';
      int ih = st.indexOf("host=");
      if (ih >= 0) pcHost = st.substring(ih + 5);
      pcStatus = "по проводу";
      pcStatusCol = C_OK;
      if (curScreen == &SCR_PC) go(&SCR_PC);
      continue;
    }
    if (c == 'D') {                       /* перерисовать текущий экран */
      if (curScreen && curScreen->draw) {
        tft.fillScreen(C_BG);      /* кадр открывается внутри */
        curScreen->draw();
      }
    } else if (c == 0x54) {               /* точка касания: x2 y2 */
      uint8_t b[4]; int n = 0;
      uint32_t t0 = millis();
      while (n < 4 && millis() - t0 < 80) {
        if (Serial.available()) b[n++] = Serial.read();
      }
      if (n != 4) continue;
      TouchPt t;
      t.x = b[0] | (b[1] << 8);
      t.y = b[2] | (b[3] << 8);
      t.ok = true;

      /* свайп открывает шторку ТОЛЬКО от верхней кромки — как на телефоне.
         Любое другое движение считается обычным нажатием. */
      static int  swStartY = -1, swLastY = -1;
      static uint32_t swAt = 0;

      if (swStartY < 0 || millis() - swAt > 700) { swStartY = t.y; swLastY = t.y; }
      swAt = millis();

      bool fromTop = (swStartY <= STATUS_H + 26);
      int  down    = t.y - swStartY;
      int  up      = swStartY - t.y;

      if (fromTop && down > 55) {              /* потянул вниз от верха */
        swStartY = -1;
        if (!shadeOpen) openShade();
        continue;
      }
      if (fromTop && up > 55) {                /* потянул вверх от верха */
        swStartY = -1;
        if (shadeOpen) closeShade();
        continue;
      }
      swLastY = t.y;                            /* обычное нажатие — отдаём экрану */

      if (!shadeOpen && shadeHandleHit(t)) { openShade(); continue; }

      if (curScreen && curScreen->touch) curScreen->touch(t);
    }
  }
}
#endif

void loop() {
  otaTick();

  /* D4 зажата 5 секунд — погасить экран. Короткое нажатие возвращает. */
  {
    static uint32_t btnAt = 0;
    static bool btnWas = false, scrOff = false;
    bool down = (digitalRead(BTN_PIN) == LOW);
    if (down && !btnWas) btnAt = millis();
    if (down && millis() - btnAt > 5000 && !scrOff) {
      blSet(1);
      tft.fillScreen(C_BG);
      scrOff = true;
    }
    if (down && scrOff && millis() - btnAt > 5400) {
      blSet(blPwm);
      scrOff = false;
      btnAt = millis();
      go(curScreen ? curScreen : &SCR_LAUNCH);
    }
    btnWas = down;
  }

  calibTick();             /* калибровка ждёт касаний */

  /* раз в 6 часов смотрим, не выложили ли новее */
  if (wifiOk && (updLast == 0 || millis() - updLast > 6UL * 3600UL * 1000UL)) {
    static uint32_t firstAt = 0;
    if (firstAt == 0) firstAt = millis() ? millis() : 1;
    if (millis() - firstAt > 8000UL) {
      updLast = millis();
      if (otaCheck(true) && ghTag.length() > 0 &&
          verNum(ghTag) > verNum(OS_VER) + 0.001f) {
        updNew = true;
        updTag = ghTag;
        if (curScreen == &SCR_LAUNCH) go(&SCR_LAUNCH);   /* перерисовать */
      }
    }
  }
  ledTick();
#if USE_USB_DISPLAY
  usbCommands();
#endif

  /* обновление строки состояния и живых экранов */
  if (millis() - lastUi > 1000) {
    lastUi = millis();
    if (WiFi.status() == WL_CONNECTED) {
      bool was = wifiOk;
      wifiOk = true;
      wifiDown = 0;
      rssiVal = WiFi.RSSI();
      boardIp = WiFi.localIP().toString();
      if (!was && !otaOn) {             /* связь вернулась — поднять приёмник */
        otaStart();
      }
    } else {
      wifiOk = false;
      if (wifiDown == 0) wifiDown = millis() ? millis() : 1;
      /* сеть пропала надолго — пробуем заново */
      if (wSsid.length() && millis() - wifiDown > 20000UL) {
        wifiDown = millis();
        WiFi.reconnect();
      }
    }

    if (curScreen == &SCR_CLOCK) {
      if (millis() - weatherAt > 600000UL) weatherFetch();
      go(&SCR_CLOCK);
    } else if (curScreen == &SCR_LAUNCH) {
      statusDraw();
    }
  }

  TouchPt t = touchGet();
  /* ручка в правом верхнем углу — открыть быстрые настройки с любого экрана */
  if (t.ok && !shadeOpen && shadeHandleHit(t)) {
    openShade();
    while (touchGet().ok) delay(5);
    return;
  }
  if (t.ok && curScreen && curScreen->touch) {
    curScreen->touch(t);
#if USE_USB_DISPLAY
    /* компьютер сам пришлёт отпускание */
    uint32_t t0 = millis();
    while (touchGet().ok && millis() - t0 < 3000) { delay(5); }
#else
    uint32_t t0 = millis();
    while (touchGet().ok && millis() - t0 < 1500) delay(6);
#endif
  }
  delay(8);
}
