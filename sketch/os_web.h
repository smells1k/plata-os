/* ══════════ ВЕБ-СТРАНИЦА ПЛАТЫ ══════════ */
/* Открывается с телефона по адресу платы.
   Здесь и управление САМОЙ платой, и управление ПК. */

extern Screen SCR_LAUNCH, SCR_CLOCK, SCR_LIGHT;
void go(const Screen *s);
void lockNow();
void logAdd(const char *what);
void cfgSave();

static String webPage() {
  String h;
  h.reserve(3200);
  h += F("<!doctype html><html lang=ru><head><meta charset=utf-8>");
  h += F("<meta name=viewport content='width=device-width,initial-scale=1,viewport-fit=cover'>");
  h += F("<title>ПЛАТА-ОС</title><style>");
  h += F("*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}");
  h += F("body{margin:0;background:#0b0f14;color:#e6edf3;font:15px system-ui,sans-serif;");
  h += F("padding:14px 12px 26px;max-width:520px;margin:0 auto}");
  h += F("h1{font-size:18px;margin:0 0 2px;display:flex;align-items:center;gap:8px}");
  h += F(".dot{width:9px;height:9px;border-radius:50%;background:#3fb950;box-shadow:0 0 8px #3fb950}");
  h += F(".dot.off{background:#f85149;box-shadow:0 0 8px #f85149}");
  h += F(".sub{color:#8b949e;font-size:12px;margin-bottom:12px}");
  h += F("h2{font-size:12px;color:#8b949e;font-weight:600;margin:16px 0 7px;letter-spacing:.5px}");
  h += F(".g{display:grid;grid-template-columns:1fr 1fr;gap:8px}");
  h += F("button{all:unset;display:flex;flex-direction:column;justify-content:center;");
  h += F("align-items:center;padding:15px 6px;border-radius:13px;background:#161b22;");
  h += F("border:1px solid #262d36;font-weight:600;font-size:13.5px;text-align:center;transition:.12s}");
  h += F("button:active{background:#243040;transform:scale(.97)}");
  h += F(".red{background:#2a1215;border-color:#5c2229;color:#ffb3b8}");
  h += F(".acc{background:#0f2233;border-color:#1d4b74;color:#8ecbff}");
  h += F(".st{margin-top:12px;padding:11px 13px;border-radius:11px;background:#11161d;");
  h += F("border:1px solid #1f2630;font-size:12.5px;color:#8b949e;min-height:42px}");
  h += F("</style></head><body>");
  h += F("<h1><span class=dot id=dot></span>ПЛАТА-ОС</h1>");
  h += "<div class=sub>" + String(OS_VER) + " · " +
       (wifiOk ? boardIp : String("нет Wi-Fi")) + "</div>";

  /* ── плата ── */
  h += F("<h2>ПЛАТА</h2><div class=g>");
  h += F("<button onclick=\"c('screen')\">Экран вкл/выкл</button>");
  h += F("<button onclick=\"c('led')\">Диод вкл/выкл</button>");
  h += F("<button onclick=\"c('light')\">Подсветка цвета</button>");
  h += F("<button onclick=\"c('bright')\">Яркость экрана</button>");
  h += F("<button onclick=\"c('lock')\">Заблокировать</button>");
  h += F("<button onclick=\"c('reboot')\">Перезагрузить плату</button>");
  h += F("<button onclick=\"c('clock')\">Часы на плате</button>");
  h += F("<button onclick=\"c('home')\">На рабочий стол</button>");
  h += F("</div>");

  /* ── ПК ── */
  h += F("<h2>КОМПЬЮТЕР</h2><div class=g>");
  h += F("<button class=acc onclick=\"c('wol')\">Разбудить ПК</button>");
  h += F("<button onclick=\"c('pclock')\">Блокировка</button>");
  h += F("<button onclick=\"c('pcsleep')\">Сон</button>");
  h += F("<button onclick=\"c('pcmute')\">Без звука</button>");
  h += F("<button onclick=\"c('pcvdown')\">Тише</button>");
  h += F("<button onclick=\"c('pcvup')\">Громче</button>");
  h += F("<button class=red onclick=\"c('pcshut')\">Выключить ПК</button>");
  h += F("<button class=red onclick=\"c('pcreboot')\">Перезагрузить ПК</button>");
  h += F("</div>");

  h += F("<div class=st id=st>готово</div>");
  h += F("<script>");
  h += F("var st=document.getElementById('st'),dot=document.getElementById('dot');");
  h += F("function c(k){st.textContent='…';fetch('/c/'+k,{cache:'no-store'})");
  h += F(".then(function(r){return r.text()}).then(function(t){st.textContent=t;ping()})");
  h += F(".catch(function(){st.textContent='плата не отвечает'})}");
  h += F("function ping(){fetch('/c/ping',{cache:'no-store'}).then(function(r){return r.text()})");
  h += F(".then(function(t){dot.className='dot';st.setAttribute('data',t)})");
  h += F(".catch(function(){dot.className='dot off'})}");
  h += F("ping();setInterval(ping,8000);");
  h += F("</script></body></html>");
  return h;
}

void webSetup() {
  otaSrv.on("/", HTTP_GET, []() {
    otaSrv.send(200, "text/html; charset=utf-8", webPage());
  });

  /* прошивка файлом */
  otaSrv.on("/ota", HTTP_GET, []() {
    String h = F("<!doctype html><meta charset=utf-8>");
    h += F("<meta name=viewport content='width=device-width,initial-scale=1'>");
    h += F("<style>body{background:#0b0f14;color:#e6edf3;font:16px system-ui;");
    h += F("text-align:center;padding:40px 20px}input{margin:24px 0}</style>");
    h += "<h2>ПЛАТА-ОС " + String(OS_VER) + "</h2>";
    h += F("<p>Выбери ПЛАТА-ОС.bin</p>");
    h += F("<form method=POST action=/update enctype=multipart/form-data>");
    h += F("<input type=file name=f><br><input type=submit value='Прошить'></form>");
    otaSrv.send(200, "text/html; charset=utf-8", h);
  });

  /* ── плата ── */
  otaSrv.on("/c/screen", HTTP_GET, []() {
    static bool off = false;
    off = !off;
    blSet(off ? 0 : blPwm);
    otaSrv.send(200, "text/plain; charset=utf-8", off ? "экран погашен" : "экран включён");
  });
  otaSrv.on("/c/led", HTTP_GET, []() {
    ledLit = !ledLit;
    if (ledLit) ledApply(); else neopixelWrite(RGB_PIN, 0, 0, 0);
    cfgSave();
    otaSrv.send(200, "text/plain; charset=utf-8", ledLit ? "диод включён" : "диод выключен");
  });
  otaSrv.on("/c/light", HTTP_GET, []() {
    /* следующий цвет по кругу */
    static const uint8_t pal[6][3] = {
      {255, 0, 0}, {0, 255, 0}, {0, 0, 255},
      {255, 120, 0}, {180, 0, 255}, {255, 255, 255}
    };
    static uint8_t k = 0;
    k = (k + 1) % 6;
    ledR = pal[k][0]; ledG = pal[k][1]; ledB = pal[k][2];
    ledMode = 0; ledLit = true;
    ledApply(); cfgSave();
    go(curScreen ? curScreen : &SCR_LAUNCH);
    otaSrv.send(200, "text/plain; charset=utf-8",
                "цвет " + String(ledR) + "," + String(ledG) + "," + String(ledB));
  });
  otaSrv.on("/c/bright", HTTP_GET, []() {
    /* яркость по кругу: 30 → 90 → 150 → 220 → 255 */
    static const uint8_t lv[5] = { 30, 90, 150, 220, 255 };
    static uint8_t k = 4;
    k = (k + 1) % 5;
    blSet(lv[k]); cfgSave();
    otaSrv.send(200, "text/plain; charset=utf-8", "яркость " + String(lv[k]));
  });
  otaSrv.on("/c/lock", HTTP_GET, []() {
    lockNow(); logAdd("заблокировано с телефона");
    otaSrv.send(200, "text/plain; charset=utf-8", "плата заблокирована");
  });
  otaSrv.on("/c/reboot", HTTP_GET, []() {
    otaSrv.send(200, "text/plain; charset=utf-8", "плата перезагружается");
    delay(400);
    logAdd("перезагрузка с телефона");
    ESP.restart();
  });
  otaSrv.on("/c/clock", HTTP_GET, []() {
    go(&SCR_CLOCK);
    otaSrv.send(200, "text/plain; charset=utf-8", "на часах");
  });
  otaSrv.on("/c/home", HTTP_GET, []() {
    go(&SCR_LAUNCH);
    otaSrv.send(200, "text/plain; charset=utf-8", "на рабочем столе");
  });

  /* ── ПК ── */
  otaSrv.on("/c/wol",     HTTP_GET, []() { sendWol(); otaSrv.send(200, "text/plain; charset=utf-8", "пакет отправлен"); });
  otaSrv.on("/c/pclock",  HTTP_GET, []() { pcDo("lock");     otaSrv.send(200, "text/plain; charset=utf-8", "ПК заблокирован"); });
  otaSrv.on("/c/pcsleep", HTTP_GET, []() { pcDo("sleep");    otaSrv.send(200, "text/plain; charset=utf-8", "ПК в сон"); });
  otaSrv.on("/c/pcshut",  HTTP_GET, []() { pcDo("shutdown"); otaSrv.send(200, "text/plain; charset=utf-8", "ПК выключается"); });
  otaSrv.on("/c/pcreboot",HTTP_GET, []() { pcDo("reboot");   otaSrv.send(200, "text/plain; charset=utf-8", "ПК перезагружается"); });
  otaSrv.on("/c/pcmute",  HTTP_GET, []() {
    pcDo("mute"); pcMuted = !pcMuted;
    otaSrv.send(200, "text/plain; charset=utf-8", pcMuted ? "звук выключен" : "звук включён");
  });
  otaSrv.on("/c/pcvup",   HTTP_GET, []() {
    pcDo("vol_up"); pcVol = min(100, pcVol + 5);
    otaSrv.send(200, "text/plain; charset=utf-8", "громче: " + String(pcVol) + "%");
  });
  otaSrv.on("/c/pcvdown", HTTP_GET, []() {
    pcDo("vol_down"); pcVol = max(0, pcVol - 5);
    otaSrv.send(200, "text/plain; charset=utf-8", "тише: " + String(pcVol) + "%");
  });

  otaSrv.on("/c/ping", HTTP_GET, []() {
    otaSrv.send(200, "text/plain; charset=utf-8", "ok");
  });
}
