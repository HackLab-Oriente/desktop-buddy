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

## Test 2 — the eye showcase, and a correction

Five renderings of the same emotion, cycling on screen. **These are options for
the group to pick from, not a proposal that one is better.**

| | |
|---|---|
| A SHIPPED | exactly what `round_face.cpp` draws today: flat colour + glow |
| B DITHERED | + vertical gradient, ordered-dithered so RGB565 stops banding |
| C CATCHLIGHT | + a specular highlight — the biggest "alive" cue in character animation |
| D DEPTH | + inner rim shading, so the eye reads as a lens not a decal |
| E LGFX NATIVE | `fillSmoothRoundRect`; **cannot** do the brow slant or squint |

Measured, ms per frame, sprite in **internal** RAM (it fits — 115 KB of ~380 KB):

| emotion | A draw | B draw | C draw | D draw | E draw | push |
|---|---|---|---|---|---|---|
| sleepy | 30.6 | 35.2 | 37.9 | 39.5 | 1.8 | 23.1 |
| happy | 62.9 | 69.6 | 73.6 | 76.0 | 2.5 | 23.1 |
| neutral | 64.3 | 76.2 | 82.6 | 87.2 | 2.6 | 23.1 |
| angry | 69.1 | 78.4 | — | — | — | 23.1 |
| curious | 80.4 | 96.2 | 104.4 | — | — | 23.1 |
| **surprised** | **102.4** | 123.8 | 134.7 | **142.7** | 3.3 | 23.1 |

### Correction: we are CPU-bound, not wire-bound

The earlier `fillScreen` benchmark said "wire-bound" — but `fillScreen` is a
memset and a push, and never touches the eye maths. With the real renderer,
**our SDF eye drawing costs 1.3×–4.4× the wire time.** A full face is 54 ms
(sleepy) to 125 ms (surprised) → roughly **8–19 fps**, and the CPU is the
bottleneck.

This is almost certainly why the face has felt sluggish. It is not the panel,
not the SPI clock, and not LovyanGFX — it is our per-pixel loop, which calls
`sqrtf` on every pixel of the eye bounding box plus a 10 px glow margin.

## Test 3 — optimising the renderer

Five changes, all in the inner loop, none of them clever:

1. **`-O2` for this component** (project is `-Os` globally — right for the
   firmware, wrong for a per-pixel loop).
2. **Skip the `sqrtf`** — `outside` is only non-zero at the four rounded
   corners. Everywhere else we were computing `sqrtf(0)` or `sqrtf(v*v)`.
3. **Hoist the per-row term** — `qy` depends only on `y`, and was being
   recomputed for every pixel in the row. Plus an early `continue` for pixels
   beyond the glow radius.
4. **Hoist the per-eye colours** — `rgb(em.r/4, ...)` was recomputed per pixel.
5. **Skip the read-modify-write where the eye is opaque** — that is most of the
   eye's pixels, and there is nothing behind them to blend against.

| emotion · variant | before | after | gain |
|---|---|---|---|
| neutral · A SHIPPED | 64.29 | **42.74** | −34% |
| happy · A SHIPPED | 62.93 | **45.81** | −27% |
| curious · A SHIPPED | 80.38 | **53.47** | −33% |
| neutral · D DEPTH | 87.15 | **66.74** | −23% |

A full neutral face is now **65.9 ms** (42.7 draw + 23.2 push) against 87.5 ms
before — about **15 fps, up from 11**. Push is untouched at 23.1 ms, as
expected; it is the wire.

**The trade, now quantified:** even optimised, our SDF renderer is ~16× the
cost of LovyanGFX's `fillSmoothRoundRect` (2.6 ms). What that buys is the brow
slant and the happy squint, which the library primitive cannot express at all.
That is the decision for the group, and it is no longer a matter of opinion.

**Biggest remaining lever, not yet tried:** cache the rendered eye. The eye
bitmap only changes on blink or emotion change — an idle saccade is a
*translation* of an unchanged shape. Blitting a cached eye at a gaze offset
would drop most frames to near zero draw cost, leaving only the 23 ms push.
Fixed-point maths and dirty-rect pushes are the next two after that.

### The gradient bug (why B looked flat on the panel)

First attempt used `k = 1.18 - 0.62t`, brightening the top of the eye. But
neutral's blue is already **255**, so scaling above 1.0 just clips: the top
third of the eye was pinned at max blue, then fell away in ~6 px steps. Flat
region followed by visible steps reads exactly as "no gradient, hard colour
changes", which is what it looked like.

Found by dumping the sprite buffer over serial rather than squinting at the
panel — the numbers showed `b5=31,31,31,31,30,29,...` immediately.

Fix: `k = 1.0 - 0.55t` (never exceeds 1.0), and dither amplitude raised from
one quantisation step to 1.5, since one step barely breaks a band at this
viewing distance. The column now ramps `g6 46→22, b5 30→14`, monotonic from
the top pixel.

For contrast, LovyanGFX's own primitive draws in **1.8–3.3 ms**, 25–40× faster
— but it cannot express the brow slant or the happy squint, because those are
carved out of the shape as *coverage*, not drawn as shapes. That is the real
trade to discuss: expressiveness vs. an order of magnitude of CPU.

### The byte-order trap (cost us an afternoon — read this one)

**LovyanGFX stores 16bpp sprites BIG-ENDIAN**, because that is the byte order
the SPI bus consumes. `getBuffer()` hands you that raw buffer. Writing native
little-endian `uint16_t` RGB565 into it — the obvious thing to do, and what
`round_face.cpp` does with its own framebuffer — is wrong, and wrong in a way
that is easy to misdiagnose:

- a **flat** colour byte-swaps to a solid but *wrong* colour (red renders as
  blue-purple), which reads like a palette bug;
- a **gradient** byte-swaps to *horizontal rainbow stripes*, because the low
  byte carrying blue becomes the red channel and cycles rapidly down the ramp.

Proved with a test card that draws the same three colours three ways. Row 1
(LovyanGFX API) and row 3 (raw, byte-swapped) were correct; row 2 (raw,
little-endian) rendered R/G/B as Blue/Red/Green. Rows 1 and 3 were confirmed
byte-identical on-device (`0x00F8` / `0xE007` / `0x1F00`), so direct buffer
writes cost nothing in quality versus the library's API — which matters,
because the SDF eye renderer needs per-pixel access and cannot use `fillRect`.

Fix: convert only at the buffer boundary (`to_store` / `from_store`), and pass
`spr.color565(r,g,b)` — never a raw 565 `uint16_t` — to any LovyanGFX call.

**Methodology note.** The first round of framebuffer screenshots was decoded
with the same little-endian assumption used to write them, so they agreed with
themselves and showed nothing. Photographs of the actual panel are what caught
this. A dump is only evidence once it has been checked against the glass at
least once.

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
- The devkit has a **second** USB-C socket behind a CH343 bridge. It is better
  for flashing (auto-reset always works) but **useless for framebuffer dumps**:
  153 KB at 115200 with no flow control loses bytes, and one lost byte
  misaligns the whole base64 stream into torn garbage. Use the native USB
  socket for anything that moves bulk data. Every dump now carries a checksum
  so corruption is detected rather than silently rendered.

## What we would NOT hand over

The eye renderer. `sd_round_rect` plus coverage multiplication is what makes
the brow slant and the happy squint work — they are carved out of the eye as
*coverage*, not drawn as shapes, and `fillSmoothRoundRect` cannot express that.
The plan if this is adopted: keep the SDF eye math, render it into an
`LGFX_Sprite` instead of our raw `s_fb`.

Note also that LovyanGFX does **not** fix the eye-colouring problem. That's
RGB565 depth banding; the panel is still 16-bit. Dithering or palette work
stays ours either way.


## Test 4 — you can have the shapes AND the frame rate

The lab's verdict on test 2: keep A/B's expressiveness, keep E's fluidity, drop
the catchlight. And a direct challenge to my claim that the library primitives
could not do the brow slant — *"I can't believe we can't have those shapes
(even using another technique)."*

**That challenge was right and my claim was wrong.** Two approaches, both work:

| variant | what it is | fps | look |
|---|---|---|---|
| A SHIPPED | today's renderer, flat colour | 15.4 | baseline |
| B GRADIENT | dithered gradient + glow | 13.0 | the preferred look |
| **C CACHED** | **B's exact pixels, SDF run only on emotion/blink change** | **32.1** | **identical to B** |
| D PRIMITIVE | `fillSmoothRoundRect` + black triangle occlusion | 39.4 | flat, no glow, shapes correct |

**C is the answer.** A saccade is a *translation* of an unchanged image, so it
costs a blit rather than a re-render. Caching the three blink openness levels
per emotion (3 x 115 KB in PSRAM) takes B from 13 fps to 32 — a 2.5x gain with
pixel-identical output. Cost is a ~110 ms rebuild when the emotion changes,
which is once per reaction and reads as a natural beat rather than a stutter.

**D proves the shapes are not exclusive to the SDF.** Draw the whole eye with
the library primitive, then paint a black triangle over the region the brow
removes — subtraction instead of coverage multiplication. The angry slant comes
out correctly. It loses the gradient and the glow, but the geometry is there,
so "the primitive cannot express the brow" was simply false.

Both C and D are near the physical ceiling: the 23.1 ms wire time caps the
panel at ~43 fps at 40 MHz SPI, so 32 vs 39 fps is a much smaller perceptual
gap than 13 vs 39. Raising the SPI clock lifts both.

**Still unresolved and independent of technique:** `browAmt = open * 0.5` slices
away half the eye height, and every variant reproduces the same wedge shapes.
That is a geometry bug, not a rendering-technique problem, and it is the
remaining reason angry/sad/suspicious look broken.
