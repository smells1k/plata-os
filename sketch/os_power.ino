/* ══════════ ПИТАНИЕ ══════════ */
/* Как в телефоне: заблокировать, перезагрузить, выключить.
   Открывается из «Дополнительно» или долгим нажатием D4. */

void drawPower() {
  topBack("ПИТАНИЕ", 16);

  struct tm t;
  char up[24] = "";
  if (nowTime(t)) snprintf(up, sizeof(up), "%02d:%02d", t.tm_hour, t.tm_min);

  box(8, 52, SCR_W - 16, 30, C_PANEL, C_LINE, 6);
  textDraw(16, 60, "работаю", F_SM, C_DIM);
  textRight(SCR_W - 16, 60, (String(millis() / 60000UL) + " мин").c_str(), F_SM, C_TXT);

  const char *nm[3] = { "ЗАБЛОКИРОВАТЬ", "ПЕРЕЗАГРУЗИТЬ", "ВЫКЛЮЧИТЬ" };
  const char *sub[3] = { "экран запрётся", "плата перезапустится", "погасить плату" };
  uint16_t col[3] = { C_ACC, C_WARN, C_ERR };

  for (int i = 0; i < 3; i++) {
    int y = 92 + i * 52;
    box(8, y, SCR_W - 16, 44, C_PANEL, col[i], 8);
    textDraw(18, y + 6, nm[i], F_MD, C_TXT);
    textDraw(18, y + 26, sub[i], F_SM, C_DIM);
    g_pwY[i] = y;
  }

  textCenter(SCR_H - 24, "удержание D4 открывает это меню", F_SM, C_DIM);
}

int g_pwY[3] = { 92, 144, 196 };

void powerTouch(TouchPt t) {
  if (topBackHit(t)) { go(&SCR_LAUNCH); return; }
  for (int i = 0; i < 3; i++) {
    if (t.y >= g_pwY[i] && t.y < g_pwY[i] + 44) {
      if (i == 0) { logAdd("заблокировано"); lockNow(); }
      else if (i == 1) { logAdd("перезагрузка"); ESP.restart(); }
      else {
        /* выключение: гасим экран, диод и уходим в глубокий сон.
           Просыпается кнопкой D4. */
        logAdd("выключено");
        tft.fillScreen(C_BG);
        blSet(0);
        neopixelWrite(RGB_PIN, 0, 0, 0);
        delay(300);
        esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_PIN, 0);   /* 0 = LOW */
        esp_deep_sleep_start();
      }
      return;
    }
  }
}

/* меню питания поверх любого экрана — вызывается долгим D4 */
void powerMenu() {
  go(&SCR_POWER);
}
