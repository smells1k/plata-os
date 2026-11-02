// Настройка TFT_eSPI под 2.8" TFT 240x320 SPI + тач XPT2046
#define USER_SETUP_LOADED

#define ILI9341_DRIVER

#define TFT_MISO -1
#define TFT_MOSI 12      /* SDI  (D12) */
#define TFT_SCLK 3       /* SCK  (D3)  */
#define TFT_CS   2       /* CS экрана  (D2) */
#define TFT_DC   13      /* DC экрана  (D13) — D9 занят флеш-памятью, нельзя */
#define TFT_RST  -1      /* RESET не подключён — сброс по питанию */
/* TFT_BL не задан: подсветка на 3V3 */

#define LOAD_GLCD
#define SPI_FREQUENCY       10000000
#define SPI_READ_FREQUENCY   8000000

#define TOUCH_CS 33
