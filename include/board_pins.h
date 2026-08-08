#pragma once

// Pin mapping for the Waveshare ESP32-S3-Touch-LCD-3.5, pulled directly from
// the vendor demo:
// https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5
//   Arduino/examples/11_lvgl_arduino_v8/11_lvgl_arduino_v8.ino
//   Arduino/examples/02_axp2101_example/02_axp2101_example.ino
//   Arduino/examples/05_pcf85063_time/05_pcf85063_time.ino
// Do not change these without re-checking the vendor source.

// --- Shared I2C bus (touch, TCA9554, AXP2101, PCF85063 all share this bus) ---
#define BOARD_I2C_SDA 8
#define BOARD_I2C_SCL 7

// --- Display (SPI, ST7796 via Arduino_GFX) ---
#define BOARD_SPI_MISO 2
#define BOARD_SPI_MOSI 1
#define BOARD_SPI_SCLK 5

#define BOARD_LCD_CS  -1 // not used by this panel
#define BOARD_LCD_DC   3
#define BOARD_LCD_RST -1 // reset is driven via the TCA9554 IO expander, not a direct GPIO
#define BOARD_LCD_BL   6

// Native panel dimensions (portrait) — always pass these to the Arduino_ST7796
// constructor as-is; it applies BOARD_LCD_ROTATION itself and swaps the
// effective width/height returned by gfx->width()/height() accordingly.
#define BOARD_LCD_NATIVE_HOR_RES 320
#define BOARD_LCD_NATIVE_VER_RES 480

// 1 = landscape (90 deg). ASSUMPTION: not verified against hardware — if the
// image appears upside-down, change to 3 instead (270 deg).
#define BOARD_LCD_ROTATION 1

// --- Touch (FT6336, via SensorLib's TouchDrvFT6X36) ---
// Uses the shared I2C bus above; no dedicated pins.

// --- TCA9554 IO expander (drives LCD reset on pin 1) ---
#define BOARD_TCA9554_ADDR 0x20
#define BOARD_TCA9554_LCD_RESET_PIN 1

// --- AXP2101 power management (shares the I2C bus above) ---
// IRQ line not broken out on this board (vendor demo uses -1 / polling).
#define BOARD_AXP2101_IRQ_PIN -1

// --- PCF85063 RTC (shares the I2C bus above) ---
