# Spike: LovyanGFX on ESP-IDF v6.0.2 + GC9A01

A throwaway project on a **second** S3 + GC9A01 rig, so the working PoC buddy
never has to be disassembled. Disposable by design — if the verdict is "no",
delete the directory.

## The question

Is LovyanGFX a better substrate for the buddy's graphics layer than our
hand-rolled `esp_lcd` + framebuffer code in
`firmware/components/expressions/round_face.cpp`?

It is **not** an LVGL alternative — LovyanGFX is drawing primitives and a panel
driver, LVGL is a widget toolkit. They compose. What LovyanGFX would replace is
*our* code: panel setup, the `put`/`blend_at`/`clear`/`push` plumbing, and the
hand-written 5×7 `kFont57`.

## Gate 0 — does it even build on ESP-IDF v6.0.2? ✅ PASSED

This was the real risk: v6 removed the legacy driver APIs that forced our own
rewrite, and LovyanGFX reaches deep into the SPI/LCD peripherals.

| | |
|---|---|
| LovyanGFX commit | `3f78b70`, 2026-07-22 (actively maintained) |
| Build result | **clean, exit 0** |
| Flash cost | **37,642 B** (27,722 text + 9,484 rodata) |
| Static RAM | 436 B |
| Warnings | 3, all the same one — see below |

37 KB is far cheaper than expected. Against the 1.86 MB of headroom in the
3 MB app partition, cost is a non-issue.

The only warning is `ledc_channel_config_t::intr_type is deprecated` in
`Light_PWM.cpp` — LovyanGFX's backlight PWM helper. Cosmetic, and avoidable
entirely by tying BL to 3V3 and setting `pin_bl = -1`.

## Setup

LovyanGFX is **not** vendored (40 MB of git history, and we may not keep it):

```bash
git clone --depth 1 https://github.com/lovyan03/LovyanGFX.git components/LovyanGFX
```

Wiring is identical to the real buddy (see `../../hardware/buddy-s3-display.md`)
so every measurement transfers without a pin translation step.

```bash
source ~/.espressif/tools/activate_idf_v6.0.2.sh
idf.py set-target esp32s3
idf.py -p /dev/cu.usbmodemXXXXXXX build flash monitor
```

**Always pass `-p`.** With two boards on the desk, a bare `idf.py flash` will
happily overwrite the working buddy.

## What the firmware tests, in order

1. **Colour order** — four labelled swatches. The word must match the colour.
   RED showing blue → flip `cfg.rgb_order`. Photo-negative → flip `cfg.invert`.
   Our `esp_lcd` path needed `invert=true` + BGR; this checks whether
   LovyanGFX gets GC9A01 right on its own.
2. **Orientation** — "BUDDY" must read left-to-right. Our `esp_lcd` path came
   up mirrored and needed `esp_lcd_panel_mirror(panel, true, false)`. If this
   is correct out of the box, that regression risk is retired.
3. **Text quality** — a small-text sample. This is the readability bar our
   5×7 font has to beat, and it's the direct answer to "smaller font, fit
   more text" without hand-rolling another bitmap font (the last one shipped
   a buffer overrun that crashed the device on long Claude replies).
4. **Frame time** — 60 full-screen pushes, reported as ms/frame and fps.
5. **Sprite cost** — a 240×240 PSRAM sprite (112 KB, same as our current
   framebuffer) drawn and pushed 60×. This is both the "fly around the buddy"
   primitive and the surface we'd render the SDF eyes into.

## Results

Fill these in from the serial log. Gate for adoption: frame time no worse than
the current `esp_lcd` path, and correct colours/orientation without a fight.

| Measurement | Result |
|---|---|
| Colour order correct out of the box | **yes** — no `rgb_order` fight |
| Orientation correct out of the box | **yes** — text reads forwards, no mirror needed |
| `fillScreen` | **24.01 ms/frame (41.6 fps)** |
| Sprite draw+push | **30.78 ms/frame (32.5 fps)** |
| PSRAM for a 240×240 sprite | **116,740 B** |
| `lcd.init` internal RAM | 2,352 B |

### The important one: we are wire-bound, not CPU-bound

240×240×2 B at 40 MHz SPI is **23.04 ms of pure wire time**. We measured
24.01 ms — LovyanGFX is achieving ~96% of theoretical bus bandwidth. There is
nothing left to optimise inside the library, and our own `esp_lcd` path pays
exactly the same 23 ms, so adopting this cannot cost us frame rate.

The only two levers on frame rate are therefore:
- **raise the SPI clock** (many GC9A01 clones run at 80 MHz → ~12 ms), and
- **push dirty rectangles instead of whole frames** (the eyes are a small
  fraction of 240×240).

Both apply equally to our current renderer. Worth knowing before anyone spends
a session "optimising the drawing code" — the drawing was never the problem.

### Bring-up notes (cost us 20 minutes, will cost you the same)

- The board arrived running firmware that presented a **TinyUSB CDC** port
  (`0x303A:0x4001`). esptool's auto-reset has nothing to talk to on that
  interface, so it fails with *"No serial data received"* even though the port
  opens fine. Manual download mode (hold BOOT, tap RESET, release BOOT) makes
  the ROM present `0x303A:0x1001` "USB JTAG_serial debug unit" instead.
- After flashing, **release BOOT before resetting** or the ROM goes straight
  back to `boot:0x0 (DOWNLOAD)` and the app never runs.
- This board's USB-C is wired to the native USB pins, so the console must be
  `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` — the default UART0 console goes
  nowhere visible.

## What we would NOT hand over

The eye renderer. `sd_round_rect` plus coverage multiplication is what makes
the brow slant and the happy squint work — they are carved out of the eye as
*coverage*, not drawn as shapes, and `fillSmoothRoundRect` cannot express that.
The plan if this is adopted: keep the SDF eye math, render it into an
`LGFX_Sprite` instead of our raw `s_fb`.

Note also that LovyanGFX does **not** fix the eye-colouring problem. That's
RGB565 depth banding; the panel is still 16-bit. Dithering or palette work
stays ours either way.
