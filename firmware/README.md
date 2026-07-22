# Buddy Zero — framework seed & PoC testbed

The event bus, Berry host, and Sense/Expression drivers here are the real
framework's first commit — written for the parts already on the desk (ESP32
DevKit V1, GME12864 OLED, RC522, LEDs) and portable to the ESP32-S3 build.

## Status

- **Verified**: `idf.py build` succeeds on **ESP-IDF v6.0.2**, target `esp32`,
  with Berry compiled in (2026-07-17). Written against the v6 driver APIs
  (`esp_driver_touch_sens`, `i2c_master`, managed `espressif/cjson`) — v5.x
  is NOT supported.
- **Verified**: event bus passes its host tests (`host_test/`).
- **Unverified on hardware**: nothing has been flashed yet — the RC522
  register dance and touch thresholds are the likely first-flash tuning spots.
- **Unverified**: `esp32s3` target (the touch driver has the hw-v2 path ready;
  try it when the S3 boards arrive).

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
idf.py set-target esp32          # DevKit V1. Real buddy: esp32s3
idf.py menuconfig                # "Buddy Zero" menu: WiFi, API key, pins
idf.py build flash monitor
```

## Wiring (DevKit V1)

| Peripheral | Pins |
|---|---|
| Petting pad | bare jumper wire on GPIO 4 (touch T0) |
| Mood LED | GPIO 2 (onboard) — or external LED + 220 Ω |
| GME12864 OLED | SDA 21, SCL 22, VCC 3V3, GND |
| RC522 | SCK 18, MISO 19, MOSI 23, CS 5, RST 27, **VCC 3V3** (5 V kills it), GND |

DevKit V1 landmines: GPIO 6–11 = flash (never), 34–39 = input-only,
0/2/12 = boot straps.

## The PoC ladder

1. **Heartbeat** — flash, watch the serial log, touch the jumper wire:
   `touch.pet` → LED breathes excited. The bus works.
2. **Proto-face** — OLED shows two parametric eyes: blinks, saccades,
   emotions. `face.emotion` payloads: neutral, happy, curious, sleepy, surprised.
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
components/expressions/  OLED face, mood LED ← actions
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
