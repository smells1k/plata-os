// Настройка TFT_eSPI под 2.8" TFT 240x320 SPI + тач XPT2046
#define USER_SETUP_LOADED

#define ILI9341_DRIVER

#define TFT_MISO -1
#define TFT_MOSI 23      /* MO   на плате */
#define TFT_SCLK 18      /* SCK  на плате */
#define TFT_CS   21      /* CS   -> SDA (21) */
#define TFT_DC   22      /* DC   -> SCL (22) */
#define TFT_RST  -1      /* RESET не подключён — сброс по питанию */
/* TFT_BL не задан: подсветка на 3V3 */

#define LOAD_GLCD
#define SPI_FREQUENCY       20000000
#define SPI_READ_FREQUENCY   8000000

#define TOUCH_CS 33
