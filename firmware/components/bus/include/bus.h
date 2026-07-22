#pragma once
// Event bus — the framework's spine. Everything is an event.
// Platform-neutral core: compiles on host (tests) and ESP-IDF alike.

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace buddy {

struct Event {
  std::string name;     // dot-namespaced: "touch.pet", "nfc.tag", "brain.reply"
  std::string payload;  // small string or JSON; big data never rides the bus
};

using HandlerId = uint32_t;
using Handler = std::function<void(const Event&)>;

class Bus {
 public:
  // Pattern is an exact name, or a prefix wildcard: "touch.*", "*"
  HandlerId subscribe(std::string pattern, Handler fn);
  void unsubscribe(HandlerId id);

  // Queues the event; delivery happens on pump() (single dispatch context,
  // so handlers never need their own locking).
  void publish(Event ev);
  void publish(std::string name, std::string payload = "") {
    publish(Event{std::move(name), std::move(payload)});
  }

  // Drain the queue, invoking matching handlers. Returns events delivered.
  // On ESP this is called by the bus task; on host, by the test loop.
  size_t pump();

  static bool matches(const std::string& pattern, const std::string& name);

 private:
  struct Sub {
    HandlerId id;
    std::string pattern;
    Handler fn;
  };
  std::mutex mu_;
  std::vector<Sub> subs_;
  std::vector<Event> queue_;
  HandlerId next_id_ = 1;
};

// Global bus accessor (one bus per buddy).
Bus& bus();

// ESP only: spawn the dispatch task that owns pump() (defined in bus_task.cpp).
void bus_start();

}  // namespace buddy
