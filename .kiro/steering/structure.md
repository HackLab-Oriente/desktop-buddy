# Project Structure

> Status: `firmware/`, `packs/`, `hardware/`, `docs/` and `spikes/` exist and
> build. `webui/` and `hub/` are still planned only. The device currently has
> no runtime configuration — WiFi and the API key are compiled in via Kconfig,
> which is the single biggest blocker to handing a built board to anyone else.

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
**Purpose**: Schematics, wiring, BOM, printable case files. Each bring-up doc
carries a "gotchas we already hit" section — the lab's institutional memory for
hardware traps, and the first thing to read before wiring anything new.

### Spikes
**Location**: `spikes/<question>/`
**Purpose**: Throwaway ESP-IDF projects that answer **one question with a
number**, on separate hardware so the working buddy is never disassembled.

The pattern that has earned its keep twice now:
- a `README.md` whose top section is the *verdict and the measurements*, not
  the method — so it can be read in thirty seconds;
- a hard gate stated up front ("if it does not compile in an hour, stop");
- external dependencies cloned, not vendored, and gitignored;
- results promoted into `docs/` or `firmware/`, and the spike left behind as
  the evidence.

**Spikes live on their own branch** (`spike/<name>`), not on `main`. Only the
*answer* is merged — the measurements and the decision, usually as a `docs/`
update. The code that produced them stays on the branch, where it can be
resurrected if someone doubts the number and ignored otherwise.

A spike is disposable by design. Deleting one when its answer is recorded
elsewhere is success, not loss.

### Submodules
Third-party C/C++ that is not on the ESP Component Registry is a **git
submodule** under `firmware/components/` (currently Berry and LovyanGFX).
Registry components go in `idf_component.yml`. A fresh clone therefore needs
`git submodule update --init`.

## Naming Conventions

- **Events**: dot-namespaced lowercase — `touch.pet`, `timer.idle_5m`,
  `brain.reply`, `voice.thinking`. Namespace = the Sense that emits it.
  Lifecycle events use the same scheme: `boot.status` carries a human-readable
  step for the splash, `boot.ready` ends it.
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
_Source of truth: `README.md`, `docs/architecture.md`. Updated: 2026-08-05_
