#pragma once
// Berry VM host: loads /flash/reflexes/main.be, bridges bus events into
// script handlers, and hot-reloads on system.reload.
//
// Threading contract: the VM is created, fed events, and reloaded ONLY from
// the bus dispatch task — the VM never needs locks.

namespace buddy {

// Returns false when Berry isn't compiled in (submodule missing) —
// main.cpp falls back to C reflexes so PoCs still run.
bool berry_host_start();

}  // namespace buddy
