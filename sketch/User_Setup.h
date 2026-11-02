// Настройка TFT_eSPI под 2.8" TFT 240x320 SPI + тач XPT2046
#define USER_SETUP_LOADED

#define ILI9341_DRIVER

#define TFT_MISO -1
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS   2       /* CS экрана  (D2) */
#define TFT_DC   4       /* DC экрана  (D4) */
#define TFT_RST  -1      /* RESET не подключён — сброс по питанию */
/* TFT_BL не задан: подсветка на 3V3 */

#define LOAD_GLCD
#define SPI_FREQUENCY       10000000
#define SPI_READ_FREQUENCY   8000000

#define TOUCH_CS 33
