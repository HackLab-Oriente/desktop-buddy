#pragma once

#include <stdint.h>

namespace buddy {

class TouchGesturesFSM {
public:
  enum class Action {
    NONE,
    DOWN,
    PET,
    POKE,
    HOLD_10S
  };

  TouchGesturesFSM(uint32_t baseline, uint32_t threshold, bool touch_raises)
      : baseline_(baseline),
        threshold_(threshold),
        touch_raises_(touch_raises),
        touching_(false),
        hold10s_fired_(false),
        confirm_(0),
        touch_start_ms_(0) {}

  Action update(uint32_t raw_val, int64_t current_ms) {
    const bool raw = touch_raises_ ? (raw_val > threshold_) : (raw_val < threshold_);

    if (raw == touching_) {
      confirm_ = 0;
      if (!touching_) {
        // Recalibrate baseline
        if (raw_val > baseline_) {
          baseline_ += (raw_val - baseline_ + 63) / 64;
        } else if (baseline_ > raw_val) {
          baseline_ -= (baseline_ - raw_val + 63) / 64;
        }
        if (baseline_ == 0) baseline_ = 1;

        // Update threshold based on touch hardware version (inferred from touch_raises)
        if (!touch_raises_) { // HW v1
          threshold_ = static_cast<uint32_t>(static_cast<uint64_t>(baseline_) * 9 / 10);
        } else {              // HW v2
          threshold_ = static_cast<uint32_t>(static_cast<uint64_t>(baseline_) * 115 / 100);
        }
      }
    } else if (++confirm_ >= 2) {
      confirm_ = 0;
      touching_ = raw;
      if (touching_) {
        touch_start_ms_ = current_ms;
        hold10s_fired_ = false;
        return Action::DOWN;
      } else {
        if (!hold10s_fired_) {
          return (current_ms - touch_start_ms_ < 400) ? Action::POKE : Action::PET;
        }
      }
    }

    if (touching_ && !hold10s_fired_ && (current_ms - touch_start_ms_ >= 10000)) {
      hold10s_fired_ = true;
      return Action::HOLD_10S;
    }

    return Action::NONE;
  }

  uint32_t get_baseline() const { return baseline_; }
  uint32_t get_threshold() const { return threshold_; }
  bool is_touching() const { return touching_; }

private:
  uint32_t baseline_;
  uint32_t threshold_;
  bool touch_raises_;
  bool touching_;
  bool hold10s_fired_;
  int confirm_;
  int64_t touch_start_ms_;
};

} // namespace buddy
