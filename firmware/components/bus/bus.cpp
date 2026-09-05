#include "bus.h"

#include <algorithm>

namespace buddy {

HandlerId Bus::subscribe(std::string pattern, Handler fn) {
  std::lock_guard<std::mutex> lock(mu_);
  HandlerId id = next_id_++;
  subs_.push_back(Sub{id, std::move(pattern), std::move(fn)});
  return id;
}

void Bus::unsubscribe(HandlerId id) {
  std::lock_guard<std::mutex> lock(mu_);
  subs_.erase(std::remove_if(subs_.begin(), subs_.end(),
                             [id](const Sub& s) { return s.id == id; }),
              subs_.end());
}

void Bus::publish(Event ev) {
  std::lock_guard<std::mutex> lock(mu_);
  queue_.push_back(std::move(ev));
}

bool Bus::matches(const std::string& pattern, const std::string& name) {
  if (pattern == "*") return true;
  if (pattern.size() >= 2 && pattern.compare(pattern.size() - 2, 2, ".*") == 0) {
    const size_t prefix_len = pattern.size() - 1;  // keep the dot
    return name.size() >= prefix_len &&
           name.compare(0, prefix_len, pattern, 0, prefix_len) == 0;
  }
  return pattern == name;
}

size_t Bus::pump() {
  std::vector<Event> batch;
  std::vector<Sub> subs_snapshot;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (queue_.empty()) return 0;   // idle: don't copy the subscriber list
    batch.swap(queue_);
    subs_snapshot = subs_;  // handlers may (un)subscribe while we dispatch
  }
  for (const Event& ev : batch) {
    for (const Sub& s : subs_snapshot) {
      if (matches(s.pattern, ev.name)) s.fn(ev);
    }
  }
  return batch.size();
}

Bus& bus() {
  static Bus instance;
  return instance;
}

}  // namespace buddy
