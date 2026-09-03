// Host-side unit test for the event bus. No ESP-IDF required:
//   c++ -std=c++17 -I../components/bus/include test_bus.cpp ../components/bus/bus.cpp && ./a.out
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "bus.h"

using buddy::Bus;
using buddy::Event;

int main() {
  Bus b;
  std::vector<std::string> seen;

  // Exact match
  b.subscribe("touch.pet", [&](const Event& e) { seen.push_back("pet:" + e.payload); });
  // Prefix wildcard
  b.subscribe("touch.*", [&](const Event& e) { seen.push_back("any-touch:" + e.name); });
  // Catch-all
  auto all = b.subscribe("*", [&](const Event& e) { seen.push_back("all:" + e.name); });

  b.publish("touch.pet", "head");
  b.publish("touch.poke");
  b.publish("nfc.tag", "uid=04a1b2");
  assert(seen.empty());  // nothing delivered before pump()

  size_t n = b.pump();
  assert(n == 3);
  assert(seen == (std::vector<std::string>{
      "pet:head", "any-touch:touch.pet", "all:touch.pet",
      "any-touch:touch.poke", "all:touch.poke",
      "all:nfc.tag"}));

  // Unsubscribe stops delivery
  seen.clear();
  b.unsubscribe(all);
  b.publish("nfc.tag");
  b.pump();
  assert(seen.empty());

  // Pattern edge cases
  assert(Bus::matches("touch.*", "touch.pet"));
  assert(!Bus::matches("touch.*", "touchy.pet"));
  assert(!Bus::matches("touch.*", "touch"));  // bare prefix is not a member
  assert(Bus::matches("*", "anything.at.all"));
  assert(!Bus::matches("touch.pet", "touch.pet.hard"));

  // Handlers publishing during pump() land in the next pump
  b.subscribe("chain.a", [&](const Event&) { b.publish("chain.b"); });
  b.subscribe("chain.b", [&](const Event&) { seen.push_back("chained"); });
  b.publish("chain.a");
  b.pump();
  assert(seen.empty());
  b.pump();
  assert(seen == std::vector<std::string>{"chained"});

  // The wildcard is RECURSIVE, and it needs the dot. Both surprised a reviewer
  // reading only the header, and the second one fails silently.
  assert(Bus::matches("touch.*", "touch.pet.hard"));
  assert(Bus::matches("touch.*", "touch."));
  assert(!Bus::matches("touch*", "touch.pet"));
  assert(!Bus::matches("touch*", "touchy"));
  assert(!Bus::matches("*.*", "a.b"));
  assert(Bus::matches("", ""));
  assert(!Bus::matches("touch.pet", "touch"));

  // Unsubscribing mid-batch does NOT stop delivery for the rest of that batch:
  // pump() dispatches from a snapshot. Documented in bus.h because anything
  // that unsubscribes and then frees what it captured is a use-after-free.
  {
    Bus c;
    std::vector<std::string> got;
    buddy::HandlerId second = 0;
    c.subscribe("x", [&](const Event&) { c.unsubscribe(second); });
    second = c.subscribe("x", [&](const Event&) { got.push_back("still here"); });
    c.publish("x");
    c.pump();
    assert(got.size() == 1);      // the snapshot still held it
    c.publish("x");
    c.pump();
    assert(got.size() == 1);      // and it is gone by the next pump
  }

  // Subscribing mid-batch does not see the batch it was added during.
  {
    Bus c;
    std::vector<std::string> got;
    c.subscribe("y", [&](const Event&) {
      c.subscribe("y", [&](const Event&) { got.push_back("late"); });
    });
    c.publish("y");
    c.publish("y");
    c.pump();
    assert(got.empty());
  }

  // The queue is capped: over kMaxQueued events are dropped and counted, not
  // queued until the allocator fails -- which with exceptions off is abort().
  {
    Bus c;
    int delivered = 0;
    c.subscribe("z", [&](const Event&) { delivered++; });
    for (size_t i = 0; i < Bus::kMaxQueued + 25; i++) c.publish("z");
    assert(c.dropped() == 25);
    assert(c.dropped() == 0);     // reading resets
    c.pump();
    assert(delivered == static_cast<int>(Bus::kMaxQueued));
  }

  // A payload with an embedded NUL keeps its length through the bus.
  {
    Bus c;
    size_t len = 0;
    c.subscribe("n", [&](const Event& ev) { len = ev.payload.size(); });
    c.publish("n", std::string("ab\0cd", 5));
    c.pump();
    assert(len == 5);
  }

  printf("bus: all tests passed\n");
  return 0;
}
