# Buddy on ESP32-S3 — first bring-up: the round face

Goal for this step: the GC9A01 round display showing the color face on the
ESP32-S3 (N16R8). Nothing else needs to be wired yet — touch, audio, RC522 are
disabled by default in this config.

## Wiring — GC9A01 (SPI) → ESP32-S3

Match the firmware defaults in `firmware/main/Kconfig.projbuild`. Wire by the
**silkscreen labels on the display**, not pin order.

| GC9A01 pin | ESP32-S3 | Notes |
|---|---|---|
| VCC | 3V3 | never 5 V |
| GND | GND | |
| SCL | GPIO 12 | SPI clock (SCLK) |
| SDA | GPIO 11 | SPI data (MOSI) |
| RES | GPIO 8 | reset |
| DC  | GPIO 9 | data/command |
| CS  | GPIO 10 | chip select |
| BLK | GPIO 7 | backlight (or tie to 3V3 and set BL = -1) |

**ESP32-S3 N16R8 pin landmines** (the reason for this pin choice):
- **GPIO 33–37 are the octal PSRAM** — never use them.
- GPIO 19/20 are USB, 0/3/45/46 are strapping pins, 26–32 are flash.
- GPIO 7–12 (chosen here) are all clear.

## Flash it

```bash
cd firmware
idf.py set-target esp32s3      # first time on S3 — regenerates config
idf.py menuconfig              # Buddy Zero → Face display backend = GC9A01
                              #             → WiFi + Anthropic key (optional)
idf.py build flash monitor
```

The display backend defaults to **GC9A01** already, so a plain build targets
the round face. To go back to the OLED PoC on a classic ESP32:
`idf.py set-target esp32` then pick SSD1306 in menuconfig.

## First-boot checklist (idf.py monitor)

1. `face` / `buddy zero is alive` in the log, no `ESP_ERROR_CHECK` aborts.
2. The round screen shows two mint eyes on black that **blink and drift**.
3. If the screen is blank: check RES/DC/CS wiring and 3V3.
4. If colors look inverted or wrong (e.g. magenta eyes): flip the two lines in
   `round_face.cpp` — `esp_lcd_panel_invert_color(..., true)` and
   `rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR` (BGR ↔ RGB). GC9A01 clones vary.
5. Mirrored/rotated: add `esp_lcd_panel_mirror(panel, x, y)` in `panel_init()`.

## What plugs in next (already have the parts)

Same S3, incremental — each is one more Sense/Expression on the bus:
- **Petting pad** — touch works on S3 (GPIO 1–14). Enable `BUDDY_PIN_TOUCH`.
- **Joystick (HW-504)** — 2 analog axes + button → a new Sense on ADC pins;
  great as a debug "look direction" control for the eyes.
- **Mic (INMP441) + amp (MAX98357A) + speaker** — the I2S audio path (chirps
  first, then the push-to-talk voice loop).
- **Buttons** — extra digital Senses.
- **RC522 / PN532** — re-enable NFC.
