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

  printf("bus: all tests passed\n");
  return 0;
}
