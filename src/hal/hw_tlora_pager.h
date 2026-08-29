#pragma once
// ════════════════════════════════════════════════════════════════════════════
// hal/hw_tlora_pager.h — Hardware pin definitions for the LilyGO T-LoRa Pager
//
// The T-LoRa Pager TFT is a pager-form-factor ESP32-S3 device featuring:
//   • 2.33" ST7796 480×222 TFT panel (landscape native)
//   • SX1262 (or optional LR1121) LoRa radio — rail powered via XL9555 expander
//   • TCA8418 matrix keyboard with backlight
//   • Rotary wheel + click for scroll/nav (quadrature-decoded)
//   • ES8311 I2S audio codec + speaker
//   • microSD on the shared LoRa SPI bus
//   • L76K GPS — power rail also controlled via XL9555 expander
//   • BQ25896 battery charger / fuel-gauge via I2C (no ADC pin)
//
// The XL9555 GPIO expander (I2C, auto-scanned 0x20-0x27) controls:
//   LoRa rail, GPS rail, keyboard enable/reset, audio amp, SD pull, etc.
//   See src/hal/xl9555.h for the shared expander driver.
//
// To use LR1121 instead of SX1262 (some regional SKUs), set:
//   -DPAGER_LORA_USE_LR1121=1  in platformio.ini
// ════════════════════════════════════════════════════════════════════════════

// ── Power & board peripherals ────────────────────────────────────────────────
#define BOARD_POWERON             -1
#define BOARD_BUZZER              -1   // Audio via ES8311 codec, not passive buzzer
#define BOARD_VEXT_ENABLE         -1
#define BOARD_VEXT_ON_LEVEL       HIGH

// ── TFT display — ST7796 480×222 (landscape native) ─────────────────────────
#define TFT_SPI_HOST          SPI2_HOST
#define TFT_SPI_SCK               35
#define TFT_SPI_MISO              33
#define TFT_SPI_MOSI              34
#define TFT_SPI_3WIRE           false
#define TFT_SPI_WRITE_HZ     40000000
#define TFT_SPI_READ_HZ      16000000
#define TFT_CS                    38
#define TFT_DC                    37
#define TFT_BL                    42
#define TFT_BL_INVERT           false
#define TFT_BL_FREQ            12000
#define TFT_BL_PWM_CH              7
#define TFT_BRIGHTNESS_DEFAULT   130
#define TFT_RST                   -1
#define TFT_PANEL_WIDTH          222   // Short axis in portrait; panel is 480-wide landscape
#define TFT_PANEL_HEIGHT         480
#define TFT_PANEL_OFFSET_X        49   // Hardware-calibrated blanking offset
#define TFT_PANEL_OFFSET_Y         0
#define TFT_INVERT              true
#define TFT_RGB_ORDER           false

// ── LoRa — SX1262 (default) or LR1121 (optional) on shared SPI2 ─────────────
// Use PAGER_LORA_USE_LR1121=1 build flag for the LR1121 hardware variant.
#define LORA_SPI_SCK              35
#define LORA_SPI_MISO             33
#define LORA_SPI_MOSI             34
#define LORA_CS                   36
#define LORA_DIO1                 14
#define LORA_RST                  47
#define LORA_BUSY                 48
#define LORA_FEM_POWER_PIN        -1
#define LORA_FEM_ENABLE_PIN       -1
#define LORA_FEM_TX_MODE_PIN      -1
#ifndef PAGER_LORA_USE_LR1121
#define PAGER_LORA_USE_LR1121      0   // 0 = SX1262,  1 = LR1121
#endif

// ── microSD — shared SPI2 bus ────────────────────────────────────────────────
// Rail/detect lines live on the XL9555 expander (register bits): SD_DET=10,
// SD_PULLEN=11, SD_EN=12. LilyGO's wiki pin table cites "GPIO12/14" — those
// are physical chip pin numbers, not register bits (verified against the
// lilygo_tlora_pager pins_arduino.h in arduino-esp32).
#define SD_CS                     21
#define HAS_SD_CARD                1

// Internal-flash storage fallback: when the SD card can't be mounted, the
// storage layer falls back to this LittleFS partition (see
// partitions_pager_fs.csv) so DM history, the node archive and config export
// keep working. SD stays first choice whenever a card mounts.
#define HAS_INTERNAL_FS_FALLBACK   1
#define INTERNAL_FS_FALLBACK_PARTITION "littlefs"

// ── NFC — ST25R3916 also hangs off shared SPI2; keep its CS deasserted ──────
#define NFC_CS                    39

// ── TCA8418 I2C matrix keyboard ──────────────────────────────────────────────
// The key map and modifier state machine live in keyboard.cpp.
#define HAS_KEYBOARD               1
#define KB_SDA                     3
#define KB_SCL                     2
#define KB_ADDR                0x34
#define KB_INT                     6   // Active-low IRQ from TCA8418
#define KB_BL                     46   // Keyboard backlight enable (active HIGH)

// ── No touch on this device ──────────────────────────────────────────────────
#define HAS_TOUCH                  0
#define TOUCH_SDA                 -1
#define TOUCH_SCL                 -1
#define TOUCH_ADDR              0x00
#define TOUCH_INT                 -1
#define TOUCH_RST                 -1
#define TOUCH_I2C_PORT             0
#define TOUCH_POLL_ENABLED         0

// ── Rotary wheel — quadrature-decoded A/B encoder + click ───────────────────
// TBALL_UP / TBALL_DOWN are the A/B encoder lines; direction is derived from
// the 2-bit gray-code transition table in keyboard.cpp.
#define HAS_TRACKBALL              1
#define TBALL_UP                  40   // Encoder line A
#define TBALL_DOWN                41   // Encoder line B
#define TBALL_LEFT                -1   // No horizontal axis
#define TBALL_RIGHT               -1
#define TBALL_CLICK                7   // Wheel press

// ── User / boot button ───────────────────────────────────────────────────────
#define USER_BUTTON_PIN            0   // BOOT button (active LOW)
#define USER_BUTTON_ACTIVE_LEVEL   LOW

// ── GPS — MIA-M10Q on UART1; rail powered via XL9555 ────────────────────────
// Host-side pins, matching gps.cpp: GPS_RX is the pin the ESP32 RECEIVES NMEA
// on (wired to the module's TX), GPS_TX drives the module's RX. Per LilyGO's
// T-LoRaPager reference: BOARD_GPS_RX_PIN=12, BOARD_GPS_TX_PIN=4.
#define HAS_GPS                    1
#define GPS_RX                    12
#define GPS_TX                     4
#define GPS_BAUD               38400

// ── ES8311 I2S audio codec ───────────────────────────────────────────────────
// The codec is managed by the arduino-audio-driver library.
#define AUDIO_CODEC_ADDR        0x18
#define AUDIO_DAC_I2S_BCK          11
#define AUDIO_DAC_I2S_WS           18
#define AUDIO_DAC_I2S_DOUT         45
#define AUDIO_DAC_I2S_DIN          17
#define AUDIO_DAC_I2S_MCLK         10
// Class-D output stage soft-start; 8ms was too short and clipped the first
// note (compare SQUARE's 250ms). 50ms settles the amp without much latency.
#define AUDIO_AMP_SETTLE_MS         50

// ── Battery — BQ25896 charger IC via I2C; no dedicated ADC pin ───────────────
// Voltage is read from the BQ25896 ADC register over I2C (battery_util.cpp).
#define BATT_ADC_PIN              -1   // No direct ADC; use BQ25896
#define BATT_DIV                2.0f
#define BATT_SENSE_ENABLE_PIN     -1
#define BATT_SENSE_ENABLE_LEVEL   LOW

// ── Radio TCXO voltage ───────────────────────────────────────────────────────
#define MESH_TCXO_V             3.0f

// ── Memory / display geometry ────────────────────────────────────────────────
#define HAS_PSRAM                  1
#define DEVICE_LCD_PORTRAIT_W    222
#define DEVICE_LCD_PORTRAIT_H    480
#define DEVICE_LCD_LANDSCAPE_W   480
#define DEVICE_LCD_LANDSCAPE_H   222
