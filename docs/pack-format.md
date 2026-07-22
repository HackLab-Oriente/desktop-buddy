# Pack Format — Draft Proposal

Status: **draft for discussion** — this becomes the `pack-format` spec
(`.kiro/specs/pack-format/`) once the team blesses the direction. It's one of
the four contract-bearing pieces (two tracks depend on it: firmware loads
packs, web UI edits them, pack authors write them).

## Principles

1. **Convention over configuration.** Reserved directories with fixed
   meanings; anything else in the pack is the author's business.
2. **Paths are pack-relative, always.** Scripts never see `/flash` or `/sd`
   mounts; the framework's asset resolver overlays both.
3. **Bytes never enter the script VM.** Berry passes paths and decisions;
   streaming, decoding, and buffers are C++'s job. The only content that
   crosses into Berry is small structured data (`asset.json`, ≤ 4 KB).
4. **Packs are shareable by construction**: no secrets, no device-specific
   state, no absolute paths. Zip the directory, that's the cartridge.
5. **Offline-first media.** Anything a pack needs at *runtime* ships in the
   pack; live LLM/TTS is for the unscripted parts.

## Directory layout

```
<pack-id>/
  pack.json                 required manifest (schema below)
  reflexes/
    main.be                 required entry point; other .be files imported from it
  faces/
    *.anim.json             animation definitions
    sprites/                sprite sheets (PNG or RGB565 .bin)
  sounds/                   small audio (chirps, effects) — flash-resident
  media/                    optional large-content library — SD-resident
```

### Storage tiers

- `pack.json`, `reflexes/`, `faces/`, `sounds/` → **flash** (LittleFS,
  `/flash/packs/<id>/`). Always present; the pack boots without SD.
- `media/` → **SD card** (FAT, `/sd/packs/<id>/media/`). Optional; if the SD
  is absent, `asset.*` calls under `media/` return nil and reflexes decide the
  fallback.
- Resolution order: flash first, then SD. Same relative path everywhere, so
  content can migrate tiers without touching scripts.

## Manifest (`pack.json`)

```json
{
  "id": "boardgame-buddy",
  "name": "Board game explainer",
  "version": "0.2.0",
  "api_level": 1,
  "author": "cafe-member",
  "language": "es",
  "brain": {
    "system_prompt": "You are the warm, funny game-night host of this café…",
    "guardrails": "Only discuss the café's board games."
  },
  "voice": { "mode": "cached-first", "voice_id": "warm_host" },
  "expressions": { "emotion_map": "faces/emotions.json" }
}
```

- `id`: kebab-case, matches the directory name.
- `api_level`: the framework's script-API version this pack targets; the
  loader refuses packs from the future, runs older ones in compat.
- `brain.system_prompt` + `guardrails`: guardrails are concatenated
  non-negotiably after the prompt — kiosk packs (public-facing) live and die
  by this field.
- `voice.mode`: `"live"` (always TTS), `"cached-first"` (play a media file if
  the reflex names one, TTS otherwise).

## Script API surface (Berry)

```berry
import buddy

buddy.on(event, handler)          # subscribe: "nfc.tag", "touch.pet", "timer.*", "brain.reply", …
buddy.emit(event, payload)        # custom events between reflexes

buddy.asset.json(rel)             # small JSON → Berry map (≤4 KB), nil if missing
buddy.asset.exists(rel)           # cheap existence probe

buddy.face.play(anim)             # by name from faces/
buddy.screen.show(rel)            # image from pack, C++ decodes/blits
buddy.sound.play(rel, done_cb)    # streamed by C++; callback on sound.done
buddy.led.mood(name)
buddy.say(text)                   # → Brain/TTS pipeline (live)
buddy.hint(text)                  # on-screen text, no TTS

buddy.lang                        # active language code, from pack + device config
buddy.pack.meta                   # own manifest as a map
```

Example — the board-game explainer's entire core flow:

```berry
buddy.on("nfc.tag", def (ev)
  if !ev.payload || !ev.payload.startswith("game:") return end
  var game = ev.payload[5..]
  var meta = buddy.asset.json(f"media/games/{game}/meta.json")
  if meta == nil
    buddy.face.play("confused")
    buddy.say("Hmm, I don't know that one yet!")
    return
  end
  buddy.face.play("storyteller")
  buddy.screen.show(f"media/games/{game}/cover.png")
  buddy.sound.play(f"media/games/{game}/narration_{buddy.lang}.mp3", def ()
    buddy.face.play("idle_happy")
    buddy.hint("Ask me anything about " + meta["title"])
  end)
end)
```

## Framework responsibilities (C++)

- **Asset resolver**: overlay lookup (flash → SD), pack-scoped, no path
  escapes (`..` rejected).
- **Audio**: `sound.play` posts an action to the bus; the audio task (core 1)
  opens the file, decodes (WAV/MP3 via helix), streams to I2S in ~8 KB chunks
  under the shared SPI bus mutex (display flushes interleave). Emits
  `sound.done` / `sound.error`. Feeds chunk RMS to the lip-sync module —
  cached narration and live TTS animate the mouth identically.
- **Images**: `screen.show` decodes PNG → LVGL canvas; oversized images are
  letterboxed onto the 240×240 logical canvas.
- **SD hygiene**: mounted read-only in normal operation; remounted rw only
  during web-UI uploads. Card absence is an event (`storage.sd.gone`), not a
  crash.

## NFC mapping: two layers

1. **Semantic payloads** (preferred): the sticker's NDEF text carries meaning
   (`game:catan`, `pack:pirate`, `mode:focus`). Written once with any phone;
   works on every buddy running a pack that understands the prefix.
2. **Device registry** (for blank/UID-only tags): web UI maps UID → synthetic
   payload. Reflexes only ever see payloads.

Security rules (restated from hardware doc): tags trigger whitelisted actions
only — never authentication, never raw text into the Brain.

## Validation & authoring

- The web UI (and a CLI for CI) validates on upload: manifest schema,
  `main.be` parses, every `asset.*`/`sound.play`/`screen.show` literal
  resolves to a file, total flash-tier size within budget.
- **Narration authoring workflow** (content packs): write/edit the script
  text → one TTS call per file at authoring time (nice voice, human-reviewed)
  → drop into `media/`. The web UI can front this ("generate narration from
  this text") so the café owner never touches a terminal.

## Open questions for the spec

- Sprite format: PNG (decode cost) vs pre-converted RGB565 (tooling cost).
- Animation definition schema (`*.anim.json`) — parametric (m5stack-avatar
  style) vs sprite-sheet, or both.
- Pack switching semantics: what state survives (volume? language?), what
  resets.
- Multi-pack: one active personality + passive "content packs," or strictly
  one pack at a time? (The café buddy suggests personality + content split
  may be worth it.)
