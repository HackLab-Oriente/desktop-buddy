# Project Structure

> Status note: pre-code. Only `docs/` exists today; the layout below is the
> agreed plan from `README.md`. Sync this file when directories materialize.

## Organization Philosophy

Split by **who touches it**, not by technology:

- The **framework** (`firmware/`) is stable C++ that lab members rarely touch.
- The **fun surface** (`packs/`) is data + scripts that everyone touches.
- Optional server pieces (`hub/`) never become load-bearing for core behavior
  (device-first, hub-optional).

## Directory Patterns

### Firmware core
**Location**: `firmware/`
**Purpose**: ESP-IDF project — drivers (Senses/Expressions), event bus, Berry
VM host, Brain adapters, web server. New hardware support = a new driver
registered on the bus, not changes to existing subsystems.

### Personality packs
**Location**: `packs/<pack-name>/`
**Purpose**: One directory per pack: `pack.json` (name, system prompt, voice
config) + `reflexes/*.be` + `faces/*` + `sounds/*`. The `default` pack doubles
as the tutorial — every framework mechanism demonstrated there. Packs never
contain secrets.

### Web UI
**Location**: `webui/`
**Purpose**: Device-hosted configuration and pack editor. Must stay small
enough to serve from ESP32 flash.

### Optional hub
**Location**: `hub/`
**Purpose**: Companion server implementing the same Brain contract as the
on-device adapter. Integrations/webhooks live here, never in firmware.

### Documentation
**Location**: `docs/`
**Purpose**: Product source of truth (architecture, hardware, services,
workshops). Decisions are marked **[decided]** vs open. Update docs when
decisions change — steering summarizes, docs decide.

### Specs
**Location**: `.kiro/specs/<feature>/`
**Purpose**: Spec-driven development for contract-bearing pieces only.
**Rule of thumb**: if two tracks (firmware/hub/packs/webui) depend on it, it
gets a spec (Brain contract, event bus, pack format, web UI API); if one
person can rewrite it in an afternoon, it doesn't.

### Hardware
**Location**: `hardware/`
**Purpose**: Schematics, wiring, BOM, printable case files.

## Naming Conventions

- **Events**: dot-namespaced lowercase — `touch.pet`, `timer.idle_5m`,
  `brain.reply`, `voice.thinking`. Namespace = the Sense that emits it.
- **Actions**: `target.verb(args)` — `face.play(anim)`, `sound.chirp(mood)`.
- **Packs**: kebab-case directory names.

## Code Organization Principles

- Everything flows through the event bus; no driver calls another driver
  directly.
- Firmware components that contain logic (bus, pack parsing, Brain protocol)
  must compile on host for unit testing; only thin driver layers are
  ESP-only.
- Offline behavior is defined per component (graceful degradation is part of
  the interface).

---
_Source of truth: `README.md` (planned layout), `docs/architecture.md`.
Updated: 2026-07-13_
