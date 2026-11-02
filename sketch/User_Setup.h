// Настройка TFT_eSPI под 2.8" TFT 240x320 SPI + тач XPT2046
#define USER_SETUP_LOADED

#define ILI9341_DRIVER

#define TFT_MISO -1
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  4
#define TFT_BL   32      /* подсветка экрана */

#define LOAD_GLCD
#define SPI_FREQUENCY       10000000
#define SPI_READ_FREQUENCY   8000000

#define TOUCH_CS 33
