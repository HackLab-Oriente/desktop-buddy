# Technology Stack

> Status: the firmware builds and runs on hardware. Decisions below that have
> been **tested** are marked as such; the rest remain intent.

## Architecture

Event-driven firmware around a central **event bus**. Four extension
primitives, and every feature must present itself as one of them:

- **Senses** (inputs → events): touch, mic level, GPIO, timers, pollers — C++ drivers.
- **Expressions** (actions → outputs): face renderer, sounds/TTS, LED, motors — C++ drivers.
- **Reflexes** (event → action logic): Berry scripts, hot-reloaded; where hacking happens.
- **Skills** (LLM tool-mediated capabilities): exposed by the active Brain; v2+.

**The Brain contract** is the single most load-bearing interface: the device
never talks to a specific AI provider, it talks to "a Brain" —
`{event, personality_context, sensor_snapshot, user_input?}` in,
`{utterance?, emotion, actions[]}` out (streamed). Three interchangeable
implementations: on-device cloud adapter (default), optional hub server, none
(offline reflexes only). Device-first, hub-optional. **Never brick.**

## Core Technologies

- **Firmware**: C/C++ on **ESP-IDF v6.0.2** (not Arduino). **v5.x is not
  supported** — v6 removed the legacy driver APIs this code is written against.
- **Behavior scripting**: Berry VM (Tasmota-proven; Lua is the fallback —
  decision by integration friction, workshop session 2).
- **Graphics** (tested, replaces the earlier LVGL intent): **LovyanGFX** for
  the panel driver, sprites, fonts and blitting; **our own** signed-distance-
  field renderer for the eyes. LVGL is deliberately *not* used — the brow slant
  and squint are carved out of the eye as *coverage*, which no widget toolkit
  or drawing primitive can express. LovyanGFX was measured at 96% of
  theoretical SPI bandwidth, so the library is never the bottleneck.
- **Target hardware**: ESP32-S3 N16R8 (16 MB flash / 8 MB PSRAM). S3 required
  (vector instructions for the v2 wake-word path).

## Key Technical Decisions

- **Cache the expensive render, blit the cheap change.** The eye SDF costs
  40–100 ms a frame, but the image only changes on a blink or an emotion
  change — a saccade is a *translation* of an unchanged image. Rendering once
  per state into PSRAM sprites and blitting took 13 fps → ~30 with identical
  pixels. Generalise the principle: when quality is expensive and state changes
  rarely, cache per state. It makes quality free at runtime, which turns
  "expensive vs fast" arguments into pure taste questions.
- **Byte-order discipline at buffer boundaries.** LovyanGFX stores 16bpp
  sprites big-endian (SPI byte order). Any code writing `getBuffer()` directly
  must convert. Getting this wrong renders flat colours wrong and gradients as
  rainbow stripes — and, worse, self-consistent debug dumps will agree with the
  bug. **Verify rendering against the physical panel at least once** before
  trusting a framebuffer capture.
- **Per-component optimisation flags.** The project builds `-Os`, which is
  right globally and wrong for per-pixel or per-token inner loops. Those
  components set `-O2` in their own `CMakeLists.txt` (measured 33% on the eye
  renderer).
- **Boot is a visible contract.** `face_start()` runs first so the splash
  covers the slow work; each step publishes `boot.status`, and `boot.ready`
  ends it. Status text must reflect real steps, never a timer — on a normal
  boot it sits on "connecting wifi" for ~4 s because that is where the time
  genuinely goes.
- **Behavior is data, not firmware**: anything a pack author touches must be
  hot-reloadable via the web UI. Recompiling to change behavior is a bug.
- **Voice v1 = push-to-talk only**: release = end of utterance (no VAD),
  touch = mute (half-duplex, no AEC). Whole-clip HTTPS POST for STT — no
  streaming STT in v1. Wake word (microWakeWord) is v2.
- **Provider picks** (see `docs/services.md`; all swappable behind the Brain):
  Claude Haiku 4.5 (LLM), Groq Whisper large-v3-turbo (STT), OpenAI
  gpt-4o-mini-tts (TTS); an OpenAI-only single-account preset must also work.
  Stream LLM replies — that's where perceived latency is won.
- **Memory discipline**: DMA + WiFi buffers in internal RAM only; TLS ≈ 50 KB
  per connection; big buffers (LVGL, audio) in PSRAM. Audio tasks pinned to
  core 1, network to core 0.
- **Secrets in NVS only**, entered via web UI. Never in packs — packs must be
  shareable without leaking keys.

## Development Standards

- Subsystems must be testable in isolation (per-subsystem bring-up firmware;
  event bus and pack parsing host-compilable with unit tests).
- Web UI is served by the device itself: no app, no cloud account, works on
  first boot via AP-mode provisioning.
- Graceful degradation is a requirement, not a feature: every network-touching
  component defines its offline behavior.

## Flash Layout

16 MB, mapped in `firmware/partitions.csv`: a 3 MB app, 4 MB LittleFS for
packs, a 4 MB **raw** `model` partition, and ~5 MB reserved for a future OTA
pair. The model partition is raw because only a raw partition can be
memory-mapped — but note the Step 0 finding: **run inference from PSRAM, not
from the mapping**. Every token reads the whole weight set, it does not fit in
the MMU cache, and copying to PSRAM at boot is 2.5× faster.

## Development Environment

ESP-IDF v6.0.2 (install as workshop pre-work — it can eat an hour per laptop).

```bash
source ~/.espressif/tools/activate_idf_v6.0.2.sh
git submodule update --init          # Berry + LovyanGFX
cd firmware && idf.py set-target esp32s3 && idf.py -p <PORT> build flash monitor
```

**Always pass `-p`.** With more than one board on the desk a bare `idf.py
flash` will happily overwrite the wrong one.

Known environment trap: if a build complains the Python environment is "not
consistent", a `python` vs `python3` symlink mismatch has crept in — run
`idf.py fullclean` once from the environment you intend to keep.

---
_Source of truth: `docs/architecture.md`, `docs/services.md`,
`docs/hardware.md`, `spikes/*/README.md`. Updated: 2026-08-05_
