#pragma once
// Web UI: join WiFi, serve the reflex editor, accept uploads, hot-reload.
//
// Emits: system.reload (—) · time.synced (—)
//
// v1 grows AP-mode provisioning, a pack manager and the event console here.
// The WiFi half belongs in a `net` component of its own — see #63.

namespace buddy {

// Blocks until connected (or 15 s timeout). Returns true when online.
bool wifi_start(const char* ssid, const char* pass);

// GET  /        → editor page for /flash/reflexes/main.be
// GET  /reflex  → that file, as text/plain
// POST /reflex  → save (atomically) and publish system.reload.
//                 Requires content-type: application/json — text/plain is on
//                 the CORS safelist and would let any page write here.
//
// Started whether or not the join succeeded: the provisioning portal exists
// for the case where it did not.
bool webui_start();

}  // namespace buddy
