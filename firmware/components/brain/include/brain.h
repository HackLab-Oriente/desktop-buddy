#pragma once
// The Brain contract, device side. PoC implementation: on-device cloud
// adapter calling the Anthropic Messages API directly.
//
// Consumes: brain.ask   (payload: free text or event description)
// Emits:    brain.reply (payload: {"utterance": "...", "emotion": "..."})
//           brain.error (payload: reason)
//
// The same two events are the seam where a hub adapter plugs in later —
// reflexes never know which brain answered.

namespace buddy {

struct BrainConfig {
  const char* api_key;        // PoC: from Kconfig. v1: NVS via web UI.
  const char* model;          // e.g. "claude-haiku-4-5"
  const char* system_prompt;  // the personality (from the pack, eventually)
};

void brain_cloud_start(const BrainConfig& cfg);

}  // namespace buddy
