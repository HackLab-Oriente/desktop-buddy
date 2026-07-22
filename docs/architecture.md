# Product & Architecture Definition

Status: proposal (pre-HackLab). Decisions marked **[decided]** reflect the
current working position; everything else is open for the group.

## 1. Product identity **[decided]**

Companion first. The buddy is a desk creature with personality — think
Tamagotchi / Anki Vector, not Alexa-with-a-face. The framework must make the
*expressive* loop excellent (react, emote, chirp, converse). Usefulness is an
extension surface, not a foundation deliverable: the framework ships the plug
(Skills), the community ships the appliances.

Consequence for scope: chirps and on-screen emotes (Animal Crossing-style) are
the buddy's *native* expressive channel — they work offline, they're free, and
they carry the personality. **Push-to-talk voice is a v1 goal on top of that**
(see §7 for why PTT specifically is tractable in 4 sessions), and wake word is
explicitly v2. When voice is unavailable (offline, no API key, pipeline broken)
the buddy degrades to chirp + text — never a brick, and the workshop demo never
depends on the riskiest subsystem.

## 2. Where the brain lives: device-first, hub-optional **[decided]**

The single protocol rule: the firmware never talks to "OpenAI" or "Anthropic"
or "the hub" — it talks to **a Brain endpoint** with one small contract:

```
device → brain:  { event, personality_context, sensor_snapshot, user_input? }
brain → device:  { utterance?, emotion, actions[] }        (streamed)
```

Three interchangeable implementations of that contract:

1. **On-device cloud adapter (default).** A thin C++ adapter formats the
   request for an LLM provider API directly over TLS. Works with only WiFi +
   an API key. This is v1.
2. **Hub brain (optional, later).** Same contract served by a companion server
   (any Pi/laptop). Pointing the device at a hub URL unlocks what a bare ESP32
   genuinely cannot do: receiving webhooks (the device is behind NAT), OAuth
   integrations (email/calendar), long-term memory, cheaper/local models.
3. **No brain (offline).** Reflexes still run. The buddy is alive, just not
   conversational. Never brick.

For "check external data" without a hub, the device can **poll** (calendar
feeds, RSS, a serverless relay) — polling is a Sense, not a Skill, and needs no
inbound connectivity.

Secrets (API keys, WiFi) live in NVS on device, entered via the web UI, never
in packs — packs must be shareable without leaking keys.

## 3. Firmware stack **[decided]**

- **ESP-IDF (C++) core** owns the hard/real-time layer: display driver + LVGL
  rendering, I2S audio in/out, touch, WiFi + TLS, the event bus, OTA, the
  embedded web server, and the Brain adapters.
- **Berry scripting VM** (the Tasmota pattern) owns the behavior layer:
  reflexes, expression mappings, idle behaviors. Scripts are uploaded /
  hot-reloaded from the web UI — changing behavior never means reflashing.
  (Lua is the fallback if Berry integration fights us; the decision criterion
  is ESP-IDF integration friction, evaluated in session 2.)
- **Why not MicroPython throughout:** lower contribution barrier, but it fights
  the audio pipeline and takes over the whole firmware; the C++ core + small VM
  split keeps a stable framework that lab members rarely touch, with all the
  fun surface in scripts.

## 4. Event bus and primitives

Everything is an event: `touch.pet`, `touch.poke`, `timer.idle_5m`,
`sense.light.dark`, `brain.reply`, `webhook.*` (hub only). Behaviors subscribe
to events and emit actions: `face.play(anim)`, `sound.chirp(mood)`,
`say(text)`, `gpio.set(...)`, `brain.ask(...)`.

- **Senses** are C++ drivers registered with the bus (touch, mic level, GPIO,
  timers, pollers). Adding a *new kind* of sensor is a C++ contribution; using
  an existing one is script-level.
- **Expressions** likewise: face renderer, sound player, LED, and later motors.
  The interface is designed so actuators slot in without core changes.
- **Reflexes** are Berry handlers — this is where 90% of hacking happens.
- **Skills** are tools exposed to the LLM by whichever Brain is active. The
  device-cloud brain ships with none (v1); the hub brain hosts community ones.

## 5. Personality packs

A pack is a directory (uploaded as zip via web UI, stored on flash/SD):

```
pack.json        name, author, version, system_prompt, voice/chirp config
reflexes/*.be    Berry scripts
faces/*          sprite sheets / animation definitions
sounds/*         chirps, effects (PCM/MP3)
```

The framework ships one well-crafted `default` pack that doubles as the
tutorial: every mechanism the framework offers, demonstrated in the pack.

**Content packs (SD storage tier).** Packs can reference a `media/` library
stored on the micro SD card (VFS makes flash vs SD transparent — it's just a
path). This enables media-heavy, *offline-first* packs; the canonical example
is the **board-game explainer** (a member's coffee shop): an NFC sticker on
each game box → tap → the buddy plays a pre-generated narration with images
and face animations, entirely from SD. Live LLM/TTS is only for follow-up
questions. Pre-generating the speech (one TTS call at authoring time, cached
forever) means zero per-play cost, instant response, and no dependency on
café WiFi — the demo works with the network down, honoring "never brick."
Kiosk-style packs like this should also pin the Brain's system prompt to the
job at hand (it's a public-facing device — topic guardrails are part of the
pack, not an afterthought).

## 6. Web UI

Served by the device itself (no app, no cloud account):

- first-boot WiFi provisioning — **AP mode → captive portal** (the guaranteed
  baseline: works with no tag, no app, no working PN532)
- settings: brain endpoint, API key, volume, name
- pack manager: upload/switch/edit packs, live-reload Berry scripts
- a chat/console tab: talk to the buddy by text, watch the event bus live
  (this doubles as the debugging tool — hacker-enjoyable)

### WiFi provisioning via NFC (enhancement, not replacement)

An NFC tag can carry WiFi credentials in the standard **Wi-Fi Simple Config
NDEF record** (`application/vnd.wfa.wsc`, the WPS credential format) — writable
by Android natively or the "NFC Tools" app (iOS via app; verify the exact iOS
path before promising it). Flow: tap tag → screen shows
`Connect to <SSID>? pet to confirm` → pet → connect + save to NVS. Deploying
several buddies to one network (the café) becomes: write one tag, tap each.

The captive portal remains the floor; this is a convenience layer on top.

**Security model — WiFi tags are the one governed exception to "tags never do
auth":**

1. WiFi credentials are parsed in the **framework layer and never enter the
   Berry `nfc.tag` event** — packs get at most a content-free
   `provision.wifi_seen` signal, never the SSID/password. This closes a
   credential-exfiltration hole (a malicious pack waiting for a WiFi tap).
2. **On-screen confirmation is mandatory** — changing the network is a
   privileged action triggered by observed data (a tag), so it's never applied
   silently. Optional stricter mode: only accept WiFi tags during a pairing
   window (first boot / hold-to-pair).
3. Caveat to state to users: the password sits on the tag in the clear
   (readable with physical possession + a phone). Fine for home/café; keep the
   tag off the buddy's exterior.

Rule of thumb: exactly one privileged tag type (WiFi provisioning), privileged
*because* the framework handles it with confirmation and zero credential
exposure to scripts. All other tags stay under "whitelisted actions only."

## 7. Voice: push-to-talk in v1, wake word in v2 **[decided]**

**Ground truth first: open-vocabulary STT cannot run on an ESP32-S3.**
Whisper-class models need ~100× the S3's memory and compute. What ESP-SR runs
locally is wake-word detection (WakeNet) and fixed ~200-phrase command sets
(MultiNet) — not dictation. So the device's job is capturing and streaming
clean audio; recognition happens in the cloud (or hub). The Brain contract
already assumes this — voice adds an audio path, not a new architecture.

### v1: push-to-talk (in the workshops)

Hold the petting pad (or button) to talk. PTT is not just scope caution — it
*deletes* the two hardest voice problems:

- **Echo/barge-in** → half-duplex. The speaker sits ~2 cm from the mic; real
  echo cancellation needs a loopback reference channel the MAX98357A doesn't
  provide. With PTT, "touching = listening" naturally mutes/ducks the buddy's
  own audio. No AEC needed.
- **Endpointing** → releasing the pad marks end-of-utterance. No VAD tuning,
  no cutting the user off mid-sentence.

Pipeline: hold → duck audio + record → stream 16 kHz/16-bit mono PCM
(32 KB/s raw — no codec needed on WiFi) to a streaming STT API → transcript to
Brain → TTS audio streamed back through the amp.

Engineering notes (where the debugging hours go):
- DMA and WiFi buffers must live in *internal* RAM (PSRAM won't do); a TLS
  connection costs ~50 KB; LVGL framebuffers go to PSRAM.
- Pin audio tasks to core 1, network/TLS to core 0.
- Latency (upload → STT → LLM → TTS → download) is realistically 1.5–3 s and
  can't be removed, only *masked*: on release, the buddy instantly emotes
  "thinking" (a face + chirp). That masking lives in the personality pack.

### v2: wake word (post-workshops)

On-device wake word via the S3's vector instructions. Caveat the lab should
know upfront: Espressif's WakeNet only ships pre-trained words — a custom
"Hey Buddy" means paying Espressif to train it. The open route is
**microWakeWord** (what Home Assistant Voice PE uses): trainable ourselves,
runs on the S3. v2 also reopens the problems PTT deleted: always-on VAD for
endpointing, and half-duplex vs. barge-in tradeoffs while the buddy is
speaking. Realtime/speech-to-speech APIs are a possible shortcut worth a
spike. Chirp+text stays first-class throughout — voice is additive.

## 8. Milestones

- **M0 — It's alive:** board bring-up, animated face, touch reactions, WiFi
  provisioning, web UI skeleton.
- **M1 — It's hackable:** event bus + Berry reflexes hot-reloaded from web UI.
- **M2 — It's a personality:** Brain contract + device-cloud adapter, chat via
  web UI, LLM-driven emotions/chirps, personality packs.
- **M3 — It listens:** push-to-talk voice loop — hold-to-talk → cloud STT →
  Brain → TTS reply, latency masked by thinking emotes.
- **M4 — It's yours:** custom case assembled, default pack polished, demo.
- **v2+ (post-workshops):** wake word (microWakeWord), hub brain, Skills,
  the **power base** module (18650 battery + IP5306-class charge/boost +
  2× MG90S servo neck, bolting onto the v1 neck-ready case), NFC pack
  cartridges (session-4 stretch if time allows), more boards.

## 9. Prior art worth studying

- **Tasmota** — Berry embedded scripting on ESP32 (the pattern for M1)
- **ESPHome** — config-driven firmware, web provisioning UX
- **wire-pod** (Anki Vector) — hub architecture for a companion robot
- **Willow / Home Assistant Voice PE** — ESP32-S3 voice pipelines
- **ElatoAI, OpenAI-realtime ESP32 demos** — direct device↔realtime-API voice
- **Stack-chan** (meganetaaan) + official **M5StackChan** (ESP32-S3) — the
  closest cousin: open case/board/firmware, TypeScript-on-Moddable host + fast
  "MODs" (validates our C++ core + hot-reload script split). Its servo pan/tilt
  neck and printed brackets are the reference for our v2 power base. Contrast
  in sovereignty: M5StackChan configures via vendor app + account + XiaoZhi
  cloud; we configure via device-hosted web UI + own API keys ("Stack-chan
  with no landlord").
- **m5stack-avatar** (MIT) — parametric face: expression states, blink timing,
  saccades, and amplitude-driven **lip-sync** (mouth opening follows TTS
  loudness). Not drop-in (M5GFX vs our LVGL) but the face model ports; our
  face renderer should adopt amplitude lip-sync from day one — cheapest
  charm-per-line in the genre.
