# Hardware: Components & Wiring (v1 reference build)

We build from scratch: pick components, wire them, design and print our own
case. This is the proposed v1 bill of materials — final choices are a session 1
group decision, but **components must be ordered before session 1**.

## Core


| Part            | Suggestion                                                                                 | Notes                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| --------------- | ------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| MCU             | **ESP32-S3 devkit, N16R8** (16 MB flash / 8 MB PSRAM)                                      | S3 is required for the voice roadmap (ESP-SR vector instructions); PSRAM is required for LVGL + audio buffers. Get the devkit with both USB ports exposed.                                                                                                                                                                                                                                                                                                   |
| Display         | **Member's choice:** 1.28" round **GC9A01** or 1.54" square **ST7789** — both 240×240, SPI | Same resolution, same wiring, same LVGL/`esp_lcd` stack — only the init sequence differs (a config flag, not a code fork), so all packs/animations work on both. Rule: author faces on a 240×240 canvas with eyes/mouth inside the inscribed circle ("round-safe area"). Avoid SSD1306-class I2C OLEDs: monochrome, 128×64, and I2C too slow for animation. Bigger rects (ST7789 2" / ILI9341 2.4–2.8", 320×240) are post-v1 — they break resolution parity. |
| Touch (petting) | ESP32-S3 **native capacitive touch pins** + copper tape/pads under the shell               | Free (no component), and "pet the shell" beats "tap the screen" for a creature. A touch-capable display is optional extra.                                                                                                                                                                                                                                                                                                                                   |
| Microphone      | **INMP441** (I2S MEMS)                                                                     | Digital, no analog fuss. v1 uses it for sound-level sensing (a Sense) and push-to-talk STT streaming; near-field desk use needs no mic array.                                                                                                                                                                                                                                                                                                                |
| Audio out       | **MAX98357A** (I2S amp) + 4Ω 3W speaker (~28mm)                                            | Chirps, effects, and TTS playback for the v1 push-to-talk loop. No loopback channel → no AEC → half-duplex by design.                                                                                                                                                                                                                                                                                                                                        |
| Extras          | WS2812 RGB LED (mood light), a spare button                                                | Cheap, expressive, good workshop filler tasks.                                                                                                                                                                                                                                                                                                                                                                                                               |


Estimated cost per buddy: **≈ €20–30** in single quantities. Order at least
one spare of everything; MEMS mics and cheap displays have casualties.

## Shopping list & order tracking

Check items off as they're ordered. **Lab decision (2026-07): build 2–3
prototypes, not one per member** — the current order covers exactly that (3
builds with spares). Once v1 is solid, the path to a polished product is a
**fabbed carrier PCB batch** (see Power & assembly), not more breadboards.

### Core components (verified 2026-07-13, ordered)

- [x] ESP32-S3 devkit N16R8, dual USB-C, 3-pack (Hosyond) — [B0F5QCK6X5](https://www.amazon.com/dp/B0F5QCK6X5)
- [x] 1.28" round GC9A01 display, 240×240 SPI, 5-pack (D-FLIFE) — [B0DCBM8KV1](https://www.amazon.com/dp/B0DCBM8KV1)
- [x] INMP441 I2S MEMS microphone, 5-pack — [B0C1C64R8S](https://www.amazon.com/dp/B0C1C64R8S)
- [x] MAX98357A I2S 3W amp breakout, 6-pack — [B0FHWB5VFW](https://www.amazon.com/dp/B0FHWB5VFW)
- [x] WS2812 12-LED ring, 5-pack — [B0C77WMM7B](https://www.amazon.com/dp/B0C77WMM7B)
  (cap brightness in firmware: 12 LEDs full-white ≈ 700 mA @ 5 V)
- [x] Speaker 4Ω 3W ultra-thin 35×25×6.8 mm, JST 1.25 pigtail, 5-pack (DWEII) — [B0F3CY5ZD2](https://www.amazon.com/dp/B0F3CY5ZD2)
  (assembly note: flush-cut the JST, strip 5 mm, tin, screw into the MAX98357A output terminal)



### Extras

- [ ] Breadboard kit, 2×830 + 2×400 tie-points + 126 jumpers (BOJACK) — [B08Y59P6D1](https://www.amazon.com/dp/B08Y59P6D1) — sessions 1–3 prototyping
- [ ] Dupont wires 120pc M-M/M-F/F-F (ELEGOO) — [B01EV70C78](https://www.amazon.com/dp/B01EV70C78) — F-F wires the display module straight to devkit pins
- [x] Copper foil tape 2"×33 ft, conductive adhesive (LOVIMAG) — [B07C6YLNYL](https://www.amazon.com/dp/B07C6YLNYL) — the capacitive petting pads
- [x] Tactile pushbuttons 6 mm, 20-pack, breadboard-friendly — [B07WF76VHT](https://www.amazon.com/dp/B07WF76VHT) — spare input / boot-mode button
- [x] Solderable breadboard (ElectroCookie) — [B07ZYNWJ1S](https://www.amazon.com/dp/B07ZYNWJ1S) — final assembly mirrors the breadboard layout 1:1
- [x] M3 heat-set inserts + screws kit, 361pc with iron tips — [B0G8JLX1HR](https://www.amazon.com/dp/B0G8JLX1HR) — screw the case together; you'll reopen it constantly
- [x] 22 AWG silicone hookup wire, 6 colors ×10 ft (Fermerry) — [B089CQHRDT](https://www.amazon.com/dp/B089CQHRDT) — touch pads, speaker runs, case-mounted parts



### Hack port hardware (to order)

The back-panel hack port needs real connector hardware, not just a case cutout:

- [ ] Pin header assortment, male + female, single/double row 2.54 mm, 138pc — [B0GLHJ3DXH](https://www.amazon.com/dp/B0GLHJ3DXH) (~$10) — the port itself is a 2×4 **female** socket (Dupont-compatible) glued into the case cutout; kit also covers header repairs everywhere else
- [ ] Qwiic/Stemma QT cable kit, JST-SH 1.0 mm to Dupont — [B08HQ1VSVL](https://www.amazon.com/dp/B08HQ1VSVL) (~$8) — adapts the huge Qwiic/Stemma I2C sensor ecosystem straight into the hack port, no crimping, no panel-mount SH connector needed

**Proposed 2×4 pin map** (freeze in session 1):

```
3V3   SDA
5V    SCL
GND   GPIO A  (ADC + touch capable)
GND   GPIO B  (ADC + touch capable)
```

I2C is the extension bus (address-based, powered, and the whole Qwiic catalog
plugs in via the adapter cables); the two raw GPIOs cover buttons, analog
sensors, or an extra touch pad. Firmware side: hack port pins are owned by the
Berry layer — digital/analog read-write and touch from scripts with zero C++;
I2C devices use driver-backed Senses.

### v1 built-in sensors (to order)

Awareness kit, built into every buddy (not hack-port add-ons). All feed the
Brain's `sensor_snapshot` (free conversational awareness) and emit bus events
for reflexes. AI agents write the drivers/state machines; the human costs are
placement, mounting, and tuning — noted per sensor.

- [ ] AHT20 + BMP280 temp/humidity/pressure combo, I2C, 5-pack — [B0G1RDY1Y8](https://www.amazon.com/dp/B0G1RDY1Y8) (~$8)
  — placement: at the intake vent, low in the shell (enclosure self-heating reads 3–5 °C high). Pressure trend = weather small talk.
- [ ] BH1750 (GY-302) ambient light, I2C, 3-pack (HiLetgo) — [B00M0F29OS](https://www.amazon.com/dp/B00M0F29OS) (~$6)
  — placement: pinhole window or peeking through the vent slots. Events: `sense.light.dark` → sleep reflex.
- [ ] MPU6050 (GY-521) accelerometer + gyro, I2C, 5-pack (AITRIP) — [B07RXQGGJX](https://www.amazon.com/dp/B07RXQGGJX) (~$10)
  — mounting: rigid, screwed or firmly glued to the shell (not floating on wires). Gesture layer (`motion.pickup`, `motion.shake`, `motion.tilt`) is AI-written but needs human play-testing to tune thresholds — budget a fun hour.
- [ ] LD2410C mmWave presence radar, UART/GPIO, 3-pack — [B0FKBF3CT4](https://www.amazon.com/dp/B0FKBF3CT4) ($19)
  — the awareness king: detects you arriving/sitting/leaving *through the PLA shell* — zero case holes. Mount facing front behind the shell wall. Uses UART, not I2C.
- [ ] VL53L0X ToF distance (laser ranging), I2C, 5-pack (Starry) — [B0DZWS6WC5](https://www.amazon.com/dp/B0DZWS6WC5) ($15) — *optional*
  — anticipation: hand approaching → excited before touch. Needs a real opening or clear window (IR laser won't shoot through PLA).

~$58 total, covers 3 builds with spares. All I2C sensors share the one bus
(one `Wire` pair, distinct addresses — no pin cost per sensor). Skipped for
now: ENS160-class air quality (needs burn-in, drifts — fine lab experiment
later), PIR (blunt instrument; the radar outclasses it).

### Power, servo prep & NFC (to order)

Power architecture parts (see *Power & assembly* below) plus the two approved
future-facing additions:

- [ ] Panel-mount USB-C extension, male→female, screw mount, 2-pack — [B0G43JGJRX](https://www.amazon.com/dp/B0G43JGJRX) ($10; **order ×2** for 3 builds + spare)
  — the case's power inlet; feeds the 5 V rail directly, devkit USB becomes debug-only
- [ ] MG90S metal-gear micro servos, 6-pack — [B0DRHX1L5Q](https://www.amazon.com/dp/B0DRHX1L5Q) ($18)
  — 2 per buddy for the v2 pan/tilt neck; bench prototyping can start right after session 4. v1 only *reserves* their pins + power headroom
- [ ] PN532 NFC reader module (I2C mode), 3-pack — [B0DTHPL3GG](https://www.amazon.com/dp/B0DTHPL3GG) ($19)
  — one more device on the shared I2C bus; reads through the PLA shell, zero holes. Session-4 stretch: tap a tag to switch personality
- [x] NTAG215 NFC stickers, 50-pack — [B0CHVWTRGC](https://www.amazon.com/dp/B0CHVWTRGC) ($13)
  — pack cartridges, mode tokens, printed totems. Rule: tags trigger whitelisted actions only — never auth, never raw Brain input
- [x] Micro SD card module, 3.3 V SPI (no level shifter), 6-pack (WWZMDiB) — [B0BV8ZQ81F](https://www.amazon.com/dp/B0BV8ZQ81F) ($7)
  — media storage for content packs (pre-generated speech, images, sounds — e.g. the board-game explainer). Shares the display's SPI bus with its own CS pin; mount read-only in normal operation, writes only during web-UI uploads. Avoid the "3.3V/5V logic converter" variants — the shifter causes the classic SD-on-ESP32 failures



### Confirm someone already has (don't order blind)

- [ ] Soldering iron + solder + flux (the iron also installs the heat-set inserts)
- [ ] Wire strippers, flush cutters
- [ ] Hot glue gun
- [ ] Multimeter
- [ ] USB-C **data** cables (charge-only cables are a classic workshop time-sink)
- [ ] **USB-C wall chargers, 15 W+ (5 V/3 A)** — one per buddy; any modern phone charger qualifies
- [ ] Micro SD cards, 8–32 GB (A1 class fine) — one per buddy; every drawer has these
- [ ] Polyfuse ~500 mA (hack port protection) + 1000 µF electrolytic capacitors (5 V rail) — classic lab-bin parts
- [ ] PLA/PETG filament for the case



## Power & assembly

**Architecture rule: loads never flow through the devkit.** Worst-case v1 draw
is ~1.1–1.5 A at 5 V (WiFi bursts + speaker + capped LED ring + radar), which
exceeds the devkit's USB connector and 5 V traces — the classic source of
"reboots when the speaker plays" ghosts.

- **Power inlet**: panel-mount USB-C on the case shell → **5 V distribution
rail** on the protoboard (star topology) with a 1000 µF electrolytic. The
devkit, amp, LED ring, radar, and hack port all tap the rail. The devkit's
own USB port is programming/debug only.
- **Supply**: 5 V/3 A (15 W+) USB-C charger per buddy. Headroom now covers the
v2 servos (~1–1.5 A stall spikes) without rework.
- **Hack port 5 V** goes through a ~500 mA polyfuse — a shorted accessory
browns out the experiment, not the buddy.
- **Firmware power governor**: LED brightness cap, max speaker volume, and
(later) servo moves are coordinated in the Expression layer so worst cases
don't stack. ~20 lines; policy, not tribal knowledge.
- **Battery = the v2 "power base" module, not the head.** An 18650 +
charge/boost board (IP5306-class: charging, 5 V boost, and load sharing in
one chip) lives in the same bolt-on base as the servo neck: battery mass
becomes ballast for the moving head, heat stays away from the temp sensor,
and the base feeds the head through the existing USB-C seam — the head can't
tell wall, power bank, or battery apart. v1.5 portability hack: any USB-C
power bank (idle draw ~200 mA sits safely above cheap power banks'
auto-shutoff threshold).
- Breadboard for sessions 1–2; move to a soldered protoboard or simple carrier
PCB for final assembly in session 4. A carrier PCB designed in parallel by
interested members is a great side-track but must not block assembly.



## Case

- 3D-printed, two-part shell around the display ("head") with speaker grille,
mic hole, and a **panel-mount USB-C inlet** (decoupled from the devkit's port —
no more aligning cutouts to a board). Capacitive pads glued inside the shell
where you'd naturally pet it (top of head).
- **Neck-ready base**: flat bottom interface with M3 screw bosses and a wire
channel, so the v2 power base (servos + battery) bolts on without reprinting
the head.
- **NFC tap zone**: PN532 mounts behind the shell (reads through 2–3 mm PLA);
print a small tap icon on the surface. Radar likewise sees through the shell —
mount facing front.
- Case design starts session 1 (measure real components), first test print by
session 2, final print between sessions 3 and 4.



## Pin budget (sanity check, ESP32-S3)

SPI display (5–6 pins), I2S mic (3), I2S amp (3), touch pads (2–4), LED (1),
button (1), I2C bus (2 — shared by all built-in sensors *and* the hack port),
radar UART (2) — comfortably within the S3's GPIO count, leaving plenty of free
GPIO broken out to a header on the case ("hack port") for member sensors and
future actuators. Breaking unused GPIO + 3V3/GND out to an external connector
is part of the v1 case design on purpose: it's the extensibility promise made
physical.