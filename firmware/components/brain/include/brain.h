#pragma once
// The Brain contract, device side.
//
// Consumes: brain.ask   (payload: free text or event description)
// Emits:    brain.reply (payload: {"emotion": "...", "utterance": "..."})
//           brain.error (payload: one of the reasons below)
//           led.mood    ("thinking" while a call is in flight)
//
// Those events are the seam where a hub adapter plugs in later; reflexes never
// know which brain answered. What is Anthropic-specific here is the endpoint,
// three headers, the body shape and the content[0].text unwrap — everything
// else is transport. A hub speaks the same events over the same transport.
//
// The local model is NOT a brain backend. It is the phrase bank's use_model
// (see docs/pack-format.md), it is per-expression, and it needs PSRAM.

#include <string>

namespace buddy {

// Why an ask produced nothing. Payload of brain.error, so a reflex can react
// differently to "no key yet" than to "the wifi is gone".
namespace brain_error {
constexpr char kOffline[] = "offline";     // no network
constexpr char kNoKey[] = "no_key";        // never configured
constexpr char kAuth[] = "auth";           // 401/403
constexpr char kRateLimit[] = "rate_limit";
constexpr char kTimeout[] = "timeout";     // transport failed
constexpr char kBadReply[] = "bad_reply";  // answered, but not our contract
constexpr char kBusy[] = "busy";           // a call is already in flight
}  // namespace brain_error

struct BrainConfig {
  std::string api_key;        // PoC: from Kconfig. v1: NVS via web UI.
  std::string model;          // e.g. "claude-haiku-4-5"
  std::string system_prompt;  // the personality (from the pack, eventually)
};

// Named for the contract, not the backend — like face_start(), which hides
// two display backends. Returns false when there is nothing to start; it still
// subscribes, so an ask always produces an answer or a brain.error.
bool brain_start(const BrainConfig& cfg);

}  // namespace buddy
