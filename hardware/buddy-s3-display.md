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
idf.py menuconfig              # Buddy Zero → WiFi + Anthropic key (optional)
idf.py build flash monitor
```

The ESP32-S3 + GC9A01 is the only target — there is no backend to choose. The
classic-ESP32 + SSD1306 PoC this grew out of was removed once the S3 landed;
it lives on in git at `4d7b12e`.

## First-boot checklist (idf.py monitor)

1. `face` / `buddy zero is alive` in the log, no `ESP_ERROR_CHECK` aborts.
2. The round screen shows two cyan eyes on black that **blink and drift**.
3. If the screen is blank: check RES/DC/CS wiring and 3V3.
4. If colors look inverted or wrong (e.g. magenta eyes): flip the two lines in
   `round_face.cpp` — `esp_lcd_panel_invert_color(..., true)` and
   `rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR` (BGR ↔ RGB). GC9A01 clones vary.

## Gotchas we already hit (so you don't lose an evening)

These are all fixed in the firmware; documented here because the next lab
member wiring an S3 will hit the same ones.

- **Stack overflow in task `main` at boot.** `app_main` brings up `esp_lcd`,
  the first framebuffer draw, and the Berry VM on one task; the default
  3584-byte stack overflows (panic points at `vApplicationStackOverflowHook`).
  Fix: `CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192` (in `sdkconfig.defaults`). The
  color face task also runs at 6144.
- **Touch reads backwards on the S3.** On the classic ESP32 a touch *lowers*
  the capacitance reading; on the **S3 it raises it** — opposite polarity. The
  driver now branches on `SOC_TOUCH_SENSOR_VERSION` (fires above baseline on
  S3, below on classic). If touch never triggers, watch the `raw=… baseline=…`
  debug line: touching should push `raw` ~30% *above* baseline.
- **Display comes up horizontally mirrored.** Text reads backwards and the
  angry/sad brow slants (and gaze) flip. Fixed with
  `esp_lcd_panel_mirror(panel, true, false)` in `panel_init()`. If a different
  clone comes up upside-down or still mirrored, adjust those two args.
- **Python `python` vs `python3` mismatch.** If a CLI build complains the env
  is "not consistent" / "configured with python3", it's because VS Code and a
  hand-sourced shell picked different Python symlinks. Run `idf.py fullclean`
  once from the environment you'll keep using, then rebuild.

## WS2812 LED ring (mood halo)

The 12-LED ring glows the **emotion's mood color** (same table as the eyes —
cyan neutral, red angry, blue sad…) and animates with `led.mood`: breathe
(calm/excited), a rotating comet (thinking), or off. It's the default mood
indicator backend on the S3.

| Ring pin | ESP32-S3 | Notes |
|---|---|---|
| VCC / 5V / VDD | **5V** (VBUS) | 12 LEDs want 5 V; 3V3 is dim and marginal |
| GND | GND | shared with the S3 — required |
| DIN / IN | GPIO **21** | data in; **use the DIN side**, not DOUT |

Notes:
- **Data at 3.3 V into a 5 V strip usually works** at this short length. If the
  first LED is flaky or wrong-colored, add a level shifter, or drop the ring's
  VCC to ~4.3 V (one series diode) to lower its logic threshold.
- **Brightness is capped in firmware** (`kMaxBright = 0.35` in `led_ring.cpp`).
  12 LEDs at full white pull ~700 mA — past what the devkit's 5 V pin likes.
  Keep the cap unless the ring gets its own 5 V supply (the v1 power rail).
- Config: `menuconfig → Buddy Zero → WS2812 mood ring`, pin `BUDDY_WS2812_PIN`
  (21), count `BUDDY_WS2812_COUNT` (12).
- Ring check on boot: `ring: WS2812 ring: 12 LEDs on GPIO 21`, then a gentle
  cyan breathe. Wrong colors (e.g. red/blue swapped) → the strip isn't GRB;
  change `LED_STRIP_COLOR_COMPONENT_FMT_GRB` to `_RGB` in `led_ring.cpp`.

## What plugs in next (already have the parts)

Same S3, incremental — each is one more Sense/Expression on the bus:
- **Petting pad** — touch works on S3 (GPIO 1–14). Enable `BUDDY_PIN_TOUCH`.
- **Mic (INMP441) + amp (MAX98357A) + speaker** — the I2S audio path (chirps
  first, then the push-to-talk voice loop).
- **Buttons** — extra digital Senses.
- **RC522 / PN532** — re-enable NFC.
