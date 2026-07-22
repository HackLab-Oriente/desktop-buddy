# Buddy Zero — Wiring Guide (ESP32 DevKit V1, 30-pin)

Matches the firmware defaults in `firmware/main/Kconfig.projbuild`.
**Follow silkscreen labels, not pin positions** — DevKit V1 clones shuffle
pin order between vendors.

## Power rails first

- Devkit `3V3` → breadboard **red** rail · Devkit `GND` → **blue** rail.
- **Everything on this build is 3.3 V. The RC522 has no 5 V tolerance — 5 V
  kills it.** Nothing connects to VIN/5V at all.

## GME12864 OLED (SSD1306, I2C)

| OLED pin | To | Wire color |
|---|---|---|
| GND | GND rail | black |
| VCC | 3V3 rail | red |
| SCL | GPIO **22** | yellow |
| SDA | GPIO **21** | orange |

## RC522 RFID reader (SPI) — 3V3 ONLY

| RC522 pin | To | Wire color |
|---|---|---|
| SDA (=CS) | GPIO **5** | purple |
| SCK | GPIO **18** | purple |
| MOSI | GPIO **23** | purple |
| MISO | GPIO **19** | purple |
| IRQ | — not connected | |
| GND | GND rail | black |
| RST | GPIO **27** | white |
| 3.3V | 3V3 rail | red |

## Petting pad

- One bare male jumper wire into GPIO **4**. That's it — the exposed metal
  end is the pad. Later: tape it to a strip of copper tape for a real pad.

## Mood LED

- Zero wiring: firmware drives the **onboard LED (GPIO 2)**.
- Optional external: GPIO 2 → 220 Ω resistor → LED anode(+), cathode(−) → GND.

## Breadboard placement tips

- The DevKit V1 is wide: straddle it across **two breadboards** (or hang one
  pin row off the board's edge) so both sides have free tie-points.
- Keep the four RC522 SPI wires short and similar in length; the reader is
  the first suspect if tag reads are flaky.
- GPIO 4 (touch) picks up noise from neighbors — route the pad wire away
  from the SPI bundle.

## First-boot checklist (idf.py monitor)

1. `rc522: MFRC522 version 0x91` (or `0x92`) — SPI wiring is right.
   `0x00`/`0xFF` = check CS/SCK/MISO/MOSI and power.
2. `touch: baseline=NNN threshold=NNN` — then touching the wire fires
   `touch.down` / `touch.pet` events in the log.
3. Eyes blinking on the OLED. No display = swap SDA/SCL (the classic).
4. Tap a fob: `rc522: tag <uid>` — copy that UID into
   `packs/zero/reflexes/main.be` via the web UI.

## Diagram as code

`buddy-zero.wireviz.yml` next to this file renders a proper harness diagram:

```bash
pip install wireviz   # needs graphviz installed
wireviz hardware/buddy-zero.wireviz.yml   # → SVG/PNG next to the file
```

## v1 note

These pin choices are PoC-only (classic ESP32). The real buddy's pin map
(ESP32-S3, GC9A01 + PN532 + I2S audio + sensors) gets frozen in workshop
session 1 — see `docs/hardware.md`.
