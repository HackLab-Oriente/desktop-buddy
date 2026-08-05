# Product Overview

Desktop Buddy is an open, hackable desktop companion built on the ESP32-S3 — a
small creature that lives on your desk, reacts to touch, chirps and emotes on a
round screen, and holds conversations driven by an LLM. It is developed by a
HackLab as a group project (4 workshop sessions, then ongoing), designed to be
user-friendly for owners and enjoyable for hackers.

**Identity rule: companion first.** The expressive loop (react, emote, chirp,
converse) is the product. Usefulness (reminders, calendar, integrations) is an
extension surface for the community, never a foundation deliverable.

## Core Capabilities

- **Expressive presence**: animated face, mood chirps, touch ("petting")
  reactions, idle behaviors — all working offline.
- **LLM-driven personality**: conversation and emotional reactions via a
  pluggable "Brain"; push-to-talk voice in v1, wake word deferred to v2.
- **Data-driven behavior**: everything that makes a buddy unique lives in a
  shareable "personality pack" (prompt + faces + chirps + reflex scripts),
  edited from a web UI and hot-reloaded — never recompiled.
- **Physical extensibility**: free GPIO broken out on the case ("hack port")
  for member-added sensors and future actuators.

## Target Use Cases

1. A fun desk companion you pet, poke, and talk to (primary).
2. A personality-pack playground — packs traded and forked like cartridges.
3. A hardware hacking platform for HackLab members (new Senses/Expressions).
4. Later (v2+): a lightweight assistant via community-built Skills on a hub.

## Value Proposition

Fully open and self-built (custom electronics + printed case, ~€20–30/buddy),
with a stable C++ framework that members rarely touch and all the fun surface
in hot-reloadable scripts and data. Degrades gracefully by design: no hub →
device-only; no network → reflexes still run. **Never brick.**

## Non-Goals (v1)

Wake word, on-device STT (technically impossible on S3), OAuth integrations,
battery power, motors. These are v2+ or community territory.

## How Decisions Get Made

The director proposes starting points; **the group settles the creature**. This
is a hard constraint on design, not a courtesy: the set of emotions, moods and
actions is a collective decision, so prefer structures the group can change
without a rewrite, and present options rather than a chosen aesthetic.

It has a concrete consequence today: the emotion table lives in C++
(`face_model.h`), which is the most dictatorial place it could be. Moving it
into pack data is both the governance fix and the natural first payload for the
configuration API.

## Open Decisions

- Whether a minimal hub is built during the 4 workshop sessions (pending team
  discussion) — the architecture supports it either way.
- Project/buddy name.
- The eye look: flat vs gradient shading. Caching made them cost the same, so
  it is purely taste. Contact sheet in `spikes/lovyangfx-gc9a01/`.
- Whether to invest in an optimised inference kernel — Step 0 put a locally
  generated utterance model in the "usable but not fast" band (see below).

## Explored, Not Yet Committed

- **Local utterance generation.** Measured on hardware: a ~1.3 M parameter
  model would run around 10 tok/s — enough for short in-character quips and
  idle murmurs, not conversation. The argument for it is that the enemy of a
  desk companion is *repetition*, not stupidity, and a local model is a
  repetition-killer that costs nothing per utterance. See
  `docs/local-model-bringup.md`.
- **PWA / push notifications.** Blocked on a trusted HTTPS origin, which the
  device cannot have on a LAN without tricks. The installable-app half works
  today over plain HTTP. See `docs/ideas-exploration.html`.

---
_Source of truth: `docs/architecture.md` (decided vs open items are marked),
`docs/workshops.md` (scope per session). Updated: 2026-08-05_
