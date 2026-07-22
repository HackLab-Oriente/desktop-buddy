# Technology Stack

> Status note: the project is pre-code (definition phase). This file records
> the *decided* stack; sync it as the first implementations land.

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

- **Firmware**: C/C++ on ESP-IDF (not Arduino) — display, audio, network,
  event bus, web server, OTA.
- **Behavior scripting**: Berry VM (Tasmota-proven; Lua is the fallback —
  decision by integration friction, workshop session 2).
- **UI rendering**: LVGL via `esp_lcd`, 240×240 target, framebuffers in PSRAM.
- **Target hardware**: ESP32-S3 N16R8 (16 MB flash / 8 MB PSRAM). S3 required
  (vector instructions for the v2 wake-word path).

## Key Technical Decisions

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

## Development Environment

ESP-IDF toolchain (install as workshop pre-work — it can eat an hour per
laptop). Build/flash/test commands: TBD when `firmware/` lands.

---
_Source of truth: `docs/architecture.md`, `docs/services.md`,
`docs/hardware.md`. Updated: 2026-07-13_
