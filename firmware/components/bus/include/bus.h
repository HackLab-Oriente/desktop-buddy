#pragma once
// Event bus — the framework's spine. Everything is an event.
// Platform-neutral core: compiles on host (tests) and ESP-IDF alike.
//
// Four things the contract promises that are easy to assume wrong:
//
//   - Events published before their subscriber exists are LOST. There is no
//     replay and no retained value. Boot order is therefore load-bearing —
//     see the header of main.cpp.
//   - Delivery is in subscription order, and that is a promise: the debug
//     tracer relies on being registered first.
//   - publish() is safe from any task. It is NOT safe from an ISR: it takes a
//     mutex and may allocate. Publish from a task the ISR wakes.
//   - unsubscribe() takes effect at the next pump(), not immediately. pump()
//     dispatches from a snapshot, so a handler removed mid-batch still
//     receives the rest of that batch. Anything that unsubscribes and then
//     frees what its lambda captured is a use-after-free.

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
  // Pattern is an exact name, or a prefix wildcard: "touch.*", "*".
  //
  // The wildcard is RECURSIVE: "touch.*" matches "touch.pet.hard", not just
  // one level. And it needs the dot — "touch*" is a valid pattern that matches
  // nothing at all, silently, which is the typo to look for when a handler
  // never fires.
  //
  // Handlers run inline on the single bus task. Stay under a few milliseconds
  // and never do I/O: anything that blocks gets its own task and answers
  // through the bus, the way brain does. A slow handler stalls every other
  // subsystem, because they are all downstream of pump().
  //
  // Two invariants worth keeping, both currently true and both one line from
  // being false: every event name and pattern is 15 bytes or less, so it fits
  // a std::string's inline buffer and publishing allocates nothing; and every
  // handler lambda captures nothing, so it fits inline in std::function and
  // the per-pump snapshot allocates nothing either.
  HandlerId subscribe(std::string pattern, Handler fn);
  void unsubscribe(HandlerId id);

  // Events dropped since the last call, and resets the count.
  size_t dropped();

  // Queues the event; delivery happens on pump() (single dispatch context,
  // so handlers never need their own locking). Over kMaxQueued the event is
  // DROPPED and counted — see dropped().
  static constexpr size_t kMaxQueued = 64;
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
  size_t dropped_ = 0;
};

// Global bus accessor (one bus per buddy).
Bus& bus();

// ESP only: spawn the dispatch task that owns pump() (defined in bus_task.cpp).
bool bus_start();

}  // namespace buddy
