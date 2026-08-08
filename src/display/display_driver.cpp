#include "display_driver.h"

#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <TCA9554.h>
#include <TouchDrvFT6X36.hpp>

#include "board_pins.h"
#include "../logging/log.h"

// Ported 1:1 from the vendor demo's bus/panel construction — see
// Arduino/examples/11_lvgl_arduino_v8/11_lvgl_arduino_v8.ino
static TCA9554 s_tca(BOARD_TCA9554_ADDR);
static Arduino_DataBus *s_bus = new Arduino_ESP32SPI(
    BOARD_LCD_DC, BOARD_LCD_CS, BOARD_SPI_SCLK, BOARD_SPI_MOSI, BOARD_SPI_MISO);
static Arduino_GFX *s_gfx = new Arduino_ST7796(
    s_bus, BOARD_LCD_RST, BOARD_LCD_ROTATION, true, BOARD_LCD_NATIVE_HOR_RES, BOARD_LCD_NATIVE_VER_RES);
static TouchDrvFT6X36 s_touch;

static lv_disp_draw_buf_t s_drawBuf;
static lv_disp_drv_t s_dispDrv;
static lv_indev_drv_t s_indevDrv;
static lv_color_t *s_buf1 = nullptr;
static lv_color_t *s_buf2 = nullptr;

static void lcdResetViaIoExpander() {
    // LCD_RST is wired through the TCA9554 IO expander, not a direct GPIO.
    s_tca.write1(BOARD_TCA9554_LCD_RESET_PIN, 1);
    delay(10);
    s_tca.write1(BOARD_TCA9554_LCD_RESET_PIN, 0);
    delay(10);
    s_tca.write1(BOARD_TCA9554_LCD_RESET_PIN, 1);
    delay(200);
}

static void dispFlush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *colorP) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
#if (LV_COLOR_16_SWAP != 0)
    s_gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)&colorP->full, w, h);
#else
    s_gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&colorP->full, w, h);
#endif
    lv_disp_flush_ready(drv);
}

static void touchpadRead(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    int16_t x[1], y[1];
    uint8_t touched = s_touch.getPoint(x, y, 1);

    if (touched) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = x[0];
        data->point.y = y[0];
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

void displayDriverInit() {
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);

    s_tca.begin();
    s_tca.pinMode1(BOARD_TCA9554_LCD_RESET_PIN, OUTPUT);
    lcdResetViaIoExpander();

    if (!s_touch.begin(Wire, FT6X36_SLAVE_ADDRESS)) {
        // TODO: surface this as a UI fault indicator too, not just the log —
        // a touch-less screen otherwise fails silently from the user's side.
        logError("displayDriverInit: FT6336 touch controller not found");
    } else {
        // The touch controller reports raw portrait coordinates regardless of
        // the panel's rotation; remapped to landscape from measured corner
        // taps: raw Y tracks screen-X directly, raw X tracks screen-Y
        // inverted. setMaxCoordinates() is required for the mirror to have
        // any effect — SensorLib's updateXY() gates mirroring on it being
        // non-zero, so mirrorY alone (without this) is silently a no-op.
        s_touch.setMaxCoordinates(BOARD_LCD_NATIVE_VER_RES, BOARD_LCD_NATIVE_HOR_RES);
        s_touch.setSwapXY(true);
        s_touch.setMirrorXY(false, true);
    }

    if (!s_gfx->begin()) {
        logError("displayDriverInit: gfx->begin() failed");
    }
    s_gfx->fillScreen(RGB565_BLACK);

    pinMode(BOARD_LCD_BL, OUTPUT);
    digitalWrite(BOARD_LCD_BL, HIGH);

    lv_init();

    // Use the GFX instance's own (rotation-adjusted) dimensions rather than
    // the native portrait macros — those stay fixed regardless of rotation.
    uint32_t screenWidth = s_gfx->width();
    uint32_t screenHeight = s_gfx->height();
    uint32_t bufSize = screenWidth * (screenHeight / 4);

    // Draw buffers live in PSRAM — the panel is 320x480x16bpp, too large for
    // two full internal-SRAM buffers alongside LVGL's own allocations.
    s_buf1 = (lv_color_t *)heap_caps_malloc(bufSize * 2, MALLOC_CAP_DEFAULT | MALLOC_CAP_8BIT);
    s_buf2 = (lv_color_t *)heap_caps_malloc(bufSize * 2, MALLOC_CAP_DEFAULT | MALLOC_CAP_8BIT);
    lv_disp_draw_buf_init(&s_drawBuf, s_buf1, s_buf2, bufSize);

    lv_disp_drv_init(&s_dispDrv);
    s_dispDrv.hor_res = screenWidth;
    s_dispDrv.ver_res = screenHeight;
    s_dispDrv.flush_cb = dispFlush;
    s_dispDrv.draw_buf = &s_drawBuf;
    lv_disp_drv_register(&s_dispDrv);

    lv_indev_drv_init(&s_indevDrv);
    s_indevDrv.type = LV_INDEV_TYPE_POINTER;
    s_indevDrv.read_cb = touchpadRead;
    lv_indev_drv_register(&s_indevDrv);
}
