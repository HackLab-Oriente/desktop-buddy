# Desktop Buddy

An open, hackable desktop companion built on the ESP32-S3 — by and for our HackLab.

## What it is

A small creature that lives on your desk. It has a face, it reacts when you touch
it, it chirps and emotes, and it has a personality — driven by an LLM. It is a
**companion first**: the fun, the expressiveness, and the personality are the
product. Usefulness (reminders, calendar, integrations) comes later as optional
skills that anyone in the community can build.

Two design commitments shape everything:

1. **The behavior is data, not firmware.** The ESP32 runs a stable C++ core (the
   framework). Everything that makes a buddy *your* buddy — its face, its moods,
   its reactions, its voice, its system prompt — is a **personality pack**:
   scripts and assets you edit from a web UI and hot-reload without recompiling.
   Personalities are shareable, forkable, tradeable — like cartridges.

2. **Device-first, hub-optional.** The buddy is fully alive standing alone: local
   reflexes work offline, and it talks to an LLM provider directly over WiFi for
   conversation. The device speaks to "a brain" through one small protocol — by
   default that brain is a cloud LLM adapter on the device itself, but the same
   endpoint can point at an optional hub server, which is where heavier tricks
   (webhooks, email/calendar integrations, long-term memory) live if the lab
   builds one. No hub, no problem: the buddy degrades gracefully, never bricks.

## Core concepts

Everything in the framework is an event on a bus, and there are four extension
primitives:

| Primitive | What it is | Examples |
|---|---|---|
| **Senses** | Inputs that emit events | touch, mic, GPIO sensors, timers, polled feeds |
| **Expressions** | Outputs that consume actions | screen face, sounds/chirps, LEDs, motors |
| **Reflexes** | Local scripted behaviors (event → action), zero-latency, offline | pet it → it purrs; dark room → it sleeps |
| **Skills** | LLM-mediated capabilities exposed as tools | set a reminder, read calendar (community-built, hub or cloud) |

A **personality pack** bundles: a system prompt, expression mappings (emotional
states → animations/sounds), idle reflexes, and voice/chirp config.

## Repository layout

```
firmware/     ESP-IDF (C++) core: drivers, event bus, Berry VM, web server
              ("Buddy Zero" seed — see firmware/README.md)
packs/        Personality packs (scripts + assets); packs/zero is the seed
webui/        The on-device configuration & pack editor UI
hub/          (later, optional) companion server for integrations & webhooks
hardware/     Schematics, wiring, BOM, 3D-printable case
docs/         Architecture, decisions, workshop plans
```

## Documentation

- [Product & architecture definition](docs/architecture.md)
- [Hardware: components & wiring](docs/hardware.md)
- [Third-party services & providers](docs/services.md)
- [Pack format (draft proposal)](docs/pack-format.md)
- [Firmware architecture (interactive)](docs/firmware-architecture.html)
- [Local model bring-up — Step 0](docs/local-model-bringup.md)
- [Idea register: open threads & dead ends (interactive)](docs/ideas-exploration.html)
- [Workshop plan (4 sessions)](docs/workshops.md)

## Status

Early ideation. This repo is the foundation being brought to the HackLab for
collective definition and build. Everything here is a proposal — argue with it.
