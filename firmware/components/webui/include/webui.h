#pragma once
// PoC web UI: join WiFi (STA), serve a one-page editor, accept reflex
// uploads, and trigger hot reload. v1 grows AP-mode provisioning, pack
// manager, and the event console on this same foundation.

namespace buddy {

// Blocks until connected (or 15 s timeout). Returns true when online.
bool wifi_start(const char* ssid, const char* pass);

// GET  /        → tiny editor page for /flash/reflexes/main.be
// POST /reflex  → save body to /flash/reflexes/main.be, publish system.reload
void webui_start();

}  // namespace buddy
