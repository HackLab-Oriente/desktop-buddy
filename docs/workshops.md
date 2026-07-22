# Workshop Plan — 4 × 4h sessions

Constraint-driven plan: each session ends with something that visibly works.
Three parallel tracks so nobody is idle: **core** (firmware/framework),
**voice** (1–2 owners, sessions 1–4 — push-to-talk is a v1 goal), and
**case** (CAD/printing). Post-workshop development continues (wake word, hub,
skills — see architecture v2+).

## Before session 1 (critical prep, ~owner: Daniel)

- [x] Order all components (see [hardware.md](hardware.md)) — lead time kills workshops *(ordered 2026-07-13)*
- [ ] This repo pushed, with a firmware skeleton that compiles and blinks/draws
- [ ] ESP-IDF toolchain install instructions tested on macOS/Linux/Windows
- [ ] One assembled breadboard prototype at home, so session 1 has a known-good reference
- [ ] Create provider accounts + API keys per [services.md](services.md)
      (recommended: Claude Haiku 4.5 + Groq Whisper + OpenAI mini-tts); prove
      them from a laptop script first (POST a WAV → transcript → LLM → TTS audio
      back), so session 2's voice spike debugs only the ESP32 side, never the
      account/API side

## Session 1 — Define & bring-up

**Goal: group owns the definition; every subsystem proven on a breadboard.**

- 45 min: pitch this definition, argue, amend, decide open items
  (display shape, name of the project/buddy)
- Split tracks:
  - *Firmware:* board bring-up — display draws, touch pad reads, mic levels
    print, speaker beeps (isolated test firmware per subsystem)
  - *Voice:* record a 3 s clip from the INMP441 and play it back through the
    MAX98357A — proves I2S in *and* out plus the duck-while-recording switch,
    which is the whole half-duplex mechanism PTT relies on
  - *Hardware/case:* measure components, sketch case, start CAD
- Close: subsystem demos, assign owners per track (voice track needs 1–2
  committed owners through session 4)

## Session 2 — The core

**Goal: it's alive — face + reactions + WiFi + web UI skeleton (M0), Berry decision made.**

- *Firmware:* event bus lands; LVGL face with 2–3 animations; touch events
  drive reactions; WiFi provisioning (AP mode) + web server serving a minimal UI
- *Spike (1–2 people):* Berry VM integration — fire an event into a script,
  script triggers an animation. Decision point: Berry vs Lua, by friction
- *Voice:* streaming spike in isolated test firmware — mic → ring buffer →
  TLS/WebSocket → streaming STT API → transcript on serial. Watch internal-RAM
  headroom (DMA + WiFi buffers can't live in PSRAM; TLS costs ~50 KB) and pin
  audio to core 1, network to core 0. Success bar: talk to the breadboard, see
  your words printed
- *Case:* first test print, fit check, iterate

## Session 3 — The personality & the voice loop

**Goal: it's hackable, it has a soul, and it hears you on the bench (M1 + M2 + M3 core).**

- *Firmware:* Berry reflexes hot-reload from web UI; pack format loading
  (pack.json + reflexes + faces + sounds)
- *Brain:* device-cloud adapter — chat with the buddy from the web UI text
  console; LLM replies drive emotion → animation + chirp
- *Voice:* wire the spike into the framework as the full PTT loop — hold pad
  → duck audio + record; release → end-of-utterance → STT → Brain → TTS
  streamed back through the amp. On release, fire `voice.thinking` on the
  event bus so the pack can mask the 1.5–3 s round-trip with an emote
- *Content:* small group crafts the `default` personality pack (prompt,
  animations, chirps, **thinking/listening faces** for the voice loop) — fun,
  non-C++ work for everyone else
- *Case:* final print queued

## Session 4 — The buddy

**Goal: assembled, cased, demoable — and it talks (M3 + M4).**

- Move from breadboard to soldered protoboard; assemble into case
- Voice polish: tune ducking levels and the thinking-emote timing inside the
  closed case (acoustics change once the speaker and mic share a shell —
  budget an hour, and remember half-duplex means never listening while chirping)
- Polish pass: boot experience, idle behaviors, default pack tuning
- Reserve the last hour for the demo + retro + roadmap vote (wake word? hub?
  actuators? what does the lab want next?)

## Risk notes

- **Scope creep is the only real risk.** Anything not in M0–M4 goes to the
  v2+ list, publicly, in session 1.
- **Voice is the riskiest v1 item — it gets a track, owners, and a fallback,
  not a bigger scope.** PTT only (release = endpoint, touch = mute; no VAD,
  no AEC, no wake word — those are v2). If the loop isn't working by the end
  of session 3, it ships as a stretch goal and the demo runs on chirp + web-UI
  chat, which the architecture keeps first-class for exactly this reason.
- ESP-IDF toolchain setup can eat an hour per laptop — do it as pre-work,
  verify in the first 30 min of session 1.
- If Berry/Lua integration stalls in session 2, M1 fallback is JSON-declared
  reflexes (trigger → action tables) with scripting moved post-workshop; the
  pack format stays the same either way.
