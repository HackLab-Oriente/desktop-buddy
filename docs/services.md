# Third-Party Services: What We Need & Recommendations

Prices verified 2026-07-13. The Brain contract keeps every one of these swappable —
these are defaults, not commitments. All keys live in device NVS, entered via the
web UI, never in packs.

## Services the buddy needs

| Service | Needed for | When | Account required? |
|---|---|---|---|
| **LLM** (the Brain) | Personality, conversation, emotion selection | v1 (M2) | Yes |
| **STT** (speech-to-text) | Push-to-talk voice input | v1 (M3) | Yes |
| **TTS** (text-to-speech) | Spoken replies | v1 (M3) | Yes |
| NTP time | Clock, timers, idle behaviors | v1 | No (pool.ntp.org, free) |
| Calendar/feeds via polling | Optional Senses (ICS, RSS) | v2 | No (public/tokened URLs) |
| Webhook relay / tunnel | Inbound events without a public IP | v2 (hub era) | Free tiers (Cloudflare Tunnel, Tailscale) |

## LLM — the Brain

| Provider / model | Price (in/out per MTok) | Why / why not |
|---|---|---|
| **Anthropic Claude Haiku 4.5** ⭐ | $1 / $5 | Fast, cheap, and characterful — personality quality is the product for a companion. Streaming + prompt caching (system prompt = personality is highly cacheable). |
| OpenAI gpt-4o-mini class | ~$0.60 / $2.40 | Slightly cheaper; main draw is one-account convenience (see stacks below). |
| Anthropic Claude Sonnet 5 | $3 / $15 ($2/$10 intro) | Overkill for chirpy small talk; nice later for Skills/tool use on the hub. |

Typical interaction ≈ 1,200 input + 150 output tokens → **~$0.002 with Haiku**,
and less with prompt caching on the personality prompt.

## STT — speech-to-text

Key insight: **push-to-talk removes the need for streaming STT in v1.** Release =
end of utterance, so the device can simply HTTPS-POST the whole recorded clip
(a few seconds of WAV) and get text back. That's radically simpler on the ESP32
than a streaming WebSocket — plain `esp_http_client` multipart POST.

| Provider | Price | Mode | ESP32 fit |
|---|---|---|---|
| **Groq Whisper large-v3-turbo** ⭐ | $0.04/hour (~$0.0007/min), 10s minimum billing | Batch POST, ~216× real-time (a 5s clip transcribes in well under a second) | Perfect for PTT: one multipart POST. Generous free tier — great for a hacklab. |
| OpenAI gpt-4o-mini-transcribe | ~$0.003/min | Batch POST (realtime variant exists) | Same simplicity; ~4× Groq's price, one-account synergy. |
| Deepgram Nova-3 | $0.0048/min streaming | WebSocket streaming, very simple protocol | The v2 upgrade path: needed when wake word arrives and utterance boundaries must be detected server-side. |

## TTS — text-to-speech

Constraint: the ESP32 wants **PCM or MP3 streamed back** (MP3 decode via
libhelix/minimp3 is cheap; raw PCM 16k/22k is even easier). All three below can
output both.

| Provider | Price | Why / why not |
|---|---|---|
| **OpenAI gpt-4o-mini-tts** ⭐ | ~$0.015/min of audio ($0.60/MTok text in, $12/MTok audio out) | Cheap, good quality, ~13 voices, supports voice "instructions" (tone/character steering — useful for personality packs). |
| ElevenLabs Flash v2.5 | $0.05/1k chars (≈$0.0075 per 150-char reply) | Best character voices + voice cloning — the "give your buddy a unique voice" option. ~3× the cost; worth it if voice identity becomes core to packs. |
| Deepgram Aura-2 | ~$0.03–0.05/1k chars | Low latency, simple API; bundle appeal if already on Deepgram for STT. |

## Cost per interaction (the number that matters)

One PTT exchange ≈ 8s of user speech + ~150-char spoken reply:

| Piece | Recommended stack | Cost |
|---|---|---|
| STT (Groq) | 8s @ $0.04/hr (10s min) | ~$0.0001 |
| LLM (Haiku 4.5) | ~1.2k in / 150 out | ~$0.002 |
| TTS (OpenAI mini-tts) | ~10s audio | ~$0.0025 |
| **Total** | | **≈ half a cent** |

At a heavy 50 interactions/day: **~$7/month per buddy**. Chirp-only interactions
(reflexes, emotes) cost nothing; text-chat via web UI costs only the LLM slice.

## Recommended stacks

- **Best quality/cost (recommended):** Claude Haiku 4.5 (brain) + Groq Whisper
  (STT) + OpenAI mini-tts (TTS). Three free-tier-friendly accounts; each piece
  is the best value in its column.
- **Fewest accounts (friend-friendly):** OpenAI only — gpt-4o-mini (brain) +
  gpt-4o-mini-transcribe + gpt-4o-mini-tts. One key to paste into the web UI.
  The web UI should support both presets; the Brain contract makes the
  provider a dropdown, not a fork.
- **Character-voice upgrade:** swap TTS to ElevenLabs Flash v2.5 when a pack
  wants a signature voice.

## Implementation notes

- **Streaming LLM replies matter more than streaming STT.** Stream the Haiku
  response so the buddy's face/emotion can react as text arrives, and TTS can
  start on the first sentence — this is where perceived latency is won.
- **Prompt caching:** keep the personality system prompt byte-stable so
  Anthropic's cache cuts input cost ~90% on repeat interactions.
- Every provider here is TLS + bearer token over plain HTTPS/WebSocket — all
  ESP32-friendly; no OAuth dance needed for v1 (OAuth-y integrations are
  exactly what the v2 hub is for).

### Sources

- [Deepgram pricing](https://deepgram.com/pricing) · [Nova-3 rates breakdown](https://brasstranscripts.com/blog/deepgram-pricing-per-minute-2025-real-time-vs-batch)
- [Groq Whisper large-v3-turbo](https://groq.com/blog/whisper-large-v3-turbo-now-available-on-groq-combining-speed-quality-for-speech-recognition) · [Groq pricing guide](https://www.eesel.ai/blog/groq-pricing)
- [OpenAI transcription pricing](https://costgoat.com/pricing/openai-transcription) · [gpt-4o-mini-tts pricing](https://tokenmix.ai/blog/gpt-4o-mini-tts-cheapest-tts-api-2026)
- [ElevenLabs API pricing](https://elevenlabs.io/pricing/api)
- Anthropic model pricing: Claude API docs (Haiku 4.5 $1/$5, Sonnet 5 $3/$15 per MTok)
