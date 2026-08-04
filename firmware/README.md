# Buddy Zero — framework seed & PoC testbed

The event bus, Berry host, and Sense/Expression drivers here are the real
framework's first commit.

## One target: ESP32-S3

| Part | Hardware | Driver |
|---|---|---|
| Face | GC9A01 1.28" round color 240×240 (SPI) | `round_face.cpp` |
| Mood | WS2812 12-LED ring | `led_ring.cpp` |
| Petting | capacitive touch, GPIO 1–14 | `touch_sense.cpp` |

Both the face and the ring render the same `face_model.h` — 8 emotions, each
carrying eye geometry *and* a mood color, so the eyes and the halo always
agree. Wiring: [../hardware/buddy-s3-display.md](../hardware/buddy-s3-display.md).

The classic-ESP32 + SSD1306 PoC that this grew out of was removed once the S3
became the only target; it lives on in git at `4d7b12e` if you ever need it.

## Status

- **Verified**: `idf.py build` succeeds on **ESP-IDF v6.0.2**, Berry compiled
  in. Written against v6 driver APIs (`esp_driver_touch_sens`, `esp_lcd` +
  `espressif/esp_lcd_gc9a01`, `espressif/led_strip` 3.x, managed
  `espressif/cjson`) — **v5.x is NOT supported**.
- **Verified**: event bus passes its host tests (`host_test/`).
- **Verified on hardware**: face, ring, touch, WiFi and the Claude brain all
  run on the S3. Gotchas we already hit are written up in the hardware doc.
- **Not yet wired**: RC522 (pin defaults are still classic-ESP32 values),
  audio, SD card.

## Setup

```bash
# 1. ESP-IDF v6.x installed (EIM installer puts the activation script at
#    ~/.espressif/tools/activate_idf_v6.0.2.sh — source it, or use the
#    ESP-IDF terminal profile).
# 2. Berry submodule + its one-time codegen:
git submodule update --init
cd firmware/components/berry_host/berry
mkdir -p generate && python3 tools/coc/coc -o generate src default -c default/berry_conf.h
#    (Without the submodule/codegen the build still succeeds; reflexes fall
#     back to C — main.cpp mirrors packs/zero/reflexes/main.be.)

cd firmware
idf.py set-target esp32s3         # first time only
idf.py menuconfig                 # "Buddy Zero": WiFi, API key, pins
idf.py build flash monitor
```

## Wiring (ESP32-S3 N16R8)

| Peripheral | Pins |
|---|---|
| Petting pad | bare jumper wire on GPIO 4 (any of GPIO 1–14) |
| GC9A01 round display | SCL 12, SDA 11, CS 10, DC 9, RST 8, BL 7, **VCC 3V3** |
| WS2812 mood ring | DIN 21, **VCC 5V**, GND shared |

S3 landmines: **GPIO 33–37 are the octal PSRAM — never touch them.**
GPIO 19/20 = USB, 26–32 = flash, 0/3/45/46 = strapping pins.

## The PoC ladder

1. **Heartbeat** — flash, watch the serial log, touch the jumper wire:
   `touch.pet` → the ring breathes excited. The bus works.
2. **Proto-face** — the round display shows two parametric eyes: blinks,
   saccades, gaze, emotions. `face.emotion` payloads: neutral, happy, curious,
   sleepy, surprised, angry, sad, suspicious.
3. **Cartridges** — tap an RFID fob: `nfc.tag` + UID in the log. Edit
   `packs/zero/reflexes/main.be` with your UIDs, upload via web UI, tap again.
4. **Brain** — with WiFi + API key configured: pet the wire, the buddy asks
   Claude, the reply's emotion drives the face (utterance in the serial log).
5. **Hot reload** — open `http://<device-ip>/`, edit reflexes in the browser,
   upload — behavior changes without reflashing. This is the framework's
   whole thesis in one button.

## Layout

```
components/bus/          event bus (host-testable — see host_test/)
components/senses/       touch pad, RC522 → events
components/expressions/  round color face, WS2812 mood ring ← actions
components/brain/        Brain contract: cloud adapter (Claude)
components/webui/        WiFi STA + reflex editor + hot reload
components/berry_host/   Berry VM, buddy.* API, event dispatch
main/                    wiring + the one framework-owned reflex (brain.reply)
../packs/zero/           Buddy Zero's Berry reflexes
```

## Host tests

```bash
cd host_test
c++ -std=c++17 -Wall -I../components/bus/include test_bus.cpp ../components/bus/bus.cpp -o test_bus
./test_bus   # expect: "bus: all tests passed"
```
