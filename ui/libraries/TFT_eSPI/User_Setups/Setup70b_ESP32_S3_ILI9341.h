// Setup for the ESP32-S3 with ILI9341 display

#define USER_SETUP_ID 70

// Choose driver
#define ILI9488_DRIVER
#define SUPPORT_JPEG
#define TFT_eSPI_ENABLE_JPG

//---------------------
// Pin configuration
//---------------------
#define TFT_CS   10    // Chip select  +
#define TFT_DC   21    // Data/Command+
#define TFT_RST  7    // Reset
#define TFT_MOSI 11    // SPI MOSI
#define TFT_SCLK 13     // SPI Clock
#define TFT_MISO 4     // SPI MISO (optional, only if you need read functions)

#define TFT_BL   14    // Backlight pin
#define TFT_BACKLIGHT_ON HIGH

// Touch (optional)
#define TOUCH_CS  5   // GPIO ของ touch
#define T_CLK     13
#define T_DIN    11
#define T_DO      4
#define T_IRQ    -1

//---------------------
// Fonts & GFX
//---------------------
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

//---------------------
// SPI Settings
//---------------------
// Use HSPI instead of default FSPI
#define USE_HSPI_PORT

// Frequency settings
#define SPI_FREQUENCY       75000000   // 40 MHz (stable for ILI9341)
#define SPI_READ_FREQUENCY  16000000   // 20 MHz max for ILI9341 read
#define SPI_TOUCH_FREQUENCY 2500000    // 2.5 MHz for touch