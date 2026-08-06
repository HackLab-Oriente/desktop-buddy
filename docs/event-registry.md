# Event registry — the bus contract

Every event that exists, what it carries, who emits it, and who owns the name.

This is the **contract between teams**. It is the reason tracks can run in
parallel: the Voice team can build against `voice.*` while Firmware is still
writing the config API, as long as both sides agree on the names. Keep this
file current — a wrong registry is worse than none.

Owner: **Firmware & architecture** (the contract). Each team owns the events
inside its own namespace — see [Namespaces](#namespaces).

## How the bus works

- `bus().publish(name, payload)` queues; delivery happens on `pump()`, run by
  the bus task. **Handlers therefore never run concurrently** and need no
  locking of their own.
- `bus().subscribe(pattern, fn)` where `pattern` is an exact name, a prefix
  wildcard (`touch.*`), or `*` for everything.
- `Event` is `{ std::string name; std::string payload; }` —
  [bus.h](../firmware/components/bus/include/bus.h). One string. Big data
  never rides the bus; pass a path or an id instead.
- Berry reflexes see the same events through `buddy.on(pattern, fn)`, with
  `ev['name']` and `ev['payload']`.

## Conventions

1. **`namespace.thing`**, lowercase, dot-separated. The namespace is the
   owning subsystem, not the destination.
2. **Name facts, not commands, for inputs.** `touch.pet` is something that
   happened. Outputs may be imperative (`face.emotion`, `led.mood`) because
   they are requests to a subsystem.
3. **Payload is one string.** Plain text where possible; JSON only where
   structure is unavoidable (today: `brain.reply`). If you need a second
   field, that is a signal to check with the contract owner first.
4. **Never block in a handler.** The pump is single-threaded; a slow handler
   stalls every other subscriber.
5. **Additive by default.** Adding an event is cheap; renaming one breaks
   every pack in the wild. Choose the name as if you cannot change it.

## Current events

### Senses — things that happened

| event | payload | emitted by | when |
|---|---|---|---|
| `touch.down` | `"pad0"` | [touch_sense.cpp](../firmware/components/senses/touch_sense.cpp) | finger makes contact |
| `touch.poke` | `"pad0"` | touch_sense | released in **< 400 ms** |
| `touch.pet` | `"pad0"` | touch_sense | released in **≥ 400 ms** |
| `nfc.tag` | hex UID, e.g. `04A2B3C4` | rc522 | tag presented |
| `time.synced` | — | wifi/SNTP | clock set after connect |

### Brain — the thinking round trip

| event | payload | emitted by | when |
|---|---|---|---|
| `brain.ask` | prompt text | reflexes | something wants a reply |
| `brain.reply` | JSON `{"utterance": "...", "emotion": "..."}` | brain_cloud | model answered |
| `brain.error` | `"no_reply"` | brain_cloud | request failed |

`brain.reply` is parsed centrally in [main.cpp](../firmware/main/main.cpp) and
fanned out to `face.emotion` + `face.say`; packs normally react to those, not
to the raw reply.

### Expressions — requests to output subsystems

| event | payload | consumed by | notes |
|---|---|---|---|
| `face.emotion` | emotion name (`happy`, `sad`…) | round_face, led_ring | the ring mirrors the face colour |
| `face.say` | text | round_face | words on screen |
| `face.look` | gaze target | round_face | **subscribed, never published** — see gaps |
| `led.mood` | `calm` \| `excited` \| `thinking` \| `off` | led_ring | animation style, not colour |

### System

| event | payload | emitted by | when |
|---|---|---|---|
| `boot.status` | short status text | main | each boot step; drives the splash line |
| `boot.ready` | — | main | boot done; face glitches into the creature |
| `system.reload` | — | web UI | reload the Berry VM (hot reload) |

## Namespaces

A team owns the names inside its prefix. **Adding an event in your own
namespace needs no permission** — open a PR that changes the code *and this
file* together. Changing or removing an event in someone else's namespace
needs that team's leader.

| prefix | owner | status |
|---|---|---|
| `touch.*`, `nfc.*`, `sense.*` | Electronics | `touch.*`, `nfc.tag` live |
| `voice.*`, `sound.*` | Voice | none yet — the track defines them |
| `face.*`, `led.*` | Personality + Firmware | live |
| `config.*` | Web UI | none yet |
| `brain.*`, `boot.*`, `system.*`, `timer.*`, `storage.*` | Firmware & architecture | partly live |

Why split it this way: if Firmware has to name every event, the other tracks
block waiting on one person. If everyone invents freely, the same idea shows
up three times under three names. Namespace ownership gives autonomy inside a
boundary, which is the only version that survives four parallel tracks.

## Known gaps

Real inconsistencies, listed so nobody rediscovers them:

1. **`face.look` is dead.** `round_face.cpp` subscribes; nothing publishes it.
   Left over from the logo-following experiment. Either wire it to gaze or
   delete the subscriber.
2. **Documented but not implemented.** `architecture.md` mentions
   `timer.idle_5m`, `sense.light.dark` and `webhook.*`; `pack-format.md`
   promises `sound.done` and `storage.sd.gone`. None exist in code. They are
   reasonable designs — they just are not real yet, and docs that describe
   absent events cost someone an afternoon.
3. **The voice loop needs events that do not exist**: at minimum
   `voice.listening` and `voice.thinking`, so a pack can cover the 1.5–3 s
   round trip with an emote. First job for the Voice team.
4. **No payload schema.** `brain.reply` already carries JSON inside the string
   field. Tolerable now; it will hurt once the web UI consumes events. If a
   second structured payload appears, revisit before a third does.
5. **The emotion vocabulary is duplicated.** `face.emotion` accepts the eight
   names in `face_model.cpp`, while `led.mood` accepts only
   `calm|excited|thinking|off`. Two overlapping vocabularies for one concept —
   whatever the group settles on for expressions has to reconcile them.
6. **`brain.error` has no subscriber.** The cloud brain publishes it on
   failure and nothing reacts, so a failed request is silent: the buddy just
   never answers. That is the one gap here that contradicts a stated
   principle ("never brick") — it should at minimum drive a face.
7. **`time.synced` has no subscriber.** Harmless today, but it means nothing
   is waiting on the clock; anything time-based will need it.

An interactive view of this file — searchable, with the real publisher and
subscriber call sites — is [event-registry.html](event-registry.html).

## Adding an event — checklist

1. Is it a **fact** (a sense) or a **request** (an output)? That picks the
   namespace.
2. Is your namespace yours? If not, talk to that leader first.
3. Add the row to this file **in the same PR** as the code.
4. Payload: the smallest string that works. A path or an id, never bytes.
5. If a pack should react to it, say so in
   [pack-format.md](pack-format.md) too — packs are a public API.
