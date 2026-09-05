#pragma once
// Berry VM host: loads /flash/reflexes/main.be, bridges bus events into script
// handlers, and hot-reloads on system.reload.
//
// Consumes: *              every event, so a pack can react to anything the
//                          firmware publishes without a firmware change
//           system.reload  rebuilds the VM; deliberately not forwarded to scripts
// Emits:    whatever a reflex publishes, except the system.*, config.*, boot.*
//           and pack.* prefixes, which are firmware's and the web UI's
//
// Threading: the VM is fed events and reloaded only from the bus dispatch
// task, so it never needs locks. The FIRST load runs on the main task, from
// berry_host_start(), before the "*" subscription exists — which is the only
// reason that is safe.
//
// A reflex is code, and it arrives over HTTP or inside a shared pack. The
// sandbox is port/berry_conf.h, not this file: the prelude is ergonomics.

namespace buddy {

// Returns false when Berry isn't compiled in (submodule missing) — main.cpp
// falls back to C reflexes so PoCs still run.
bool berry_host_start();

}  // namespace buddy
