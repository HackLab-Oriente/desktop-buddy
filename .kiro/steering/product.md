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

## Open Decisions

- Whether a minimal hub is built during the 4 workshop sessions (pending team
  discussion) — the architecture supports it either way.
- Project/buddy name; final display shape vote (round GC9A01 is the default).

---
_Source of truth: `docs/architecture.md` (decided vs open items are marked),
`docs/workshops.md` (scope per session). Updated: 2026-07-13_
