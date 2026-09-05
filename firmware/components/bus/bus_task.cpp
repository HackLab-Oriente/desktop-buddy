// ESP-side dispatch loop: one task owns pump(), so all handlers run in a
// single context and never need their own locking.
#include "bus.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace buddy {
namespace {

TaskHandle_t s_task = nullptr;

void bus_task(void*) {
  int quiet = 0;
  for (;;) {
    if (bus().pump() == 0) {
      vTaskDelay(pdMS_TO_TICKS(10));
      // Reporting drops needs a moment when the bus is not busy; from inside
      // pump() it would log while the thing causing the drops is still running.
      if (++quiet >= 100) {
        quiet = 0;
        const size_t n = bus().dropped();
        if (n) ESP_LOGW("bus", "%u events dropped — a handler or a reflex is "
                               "publishing faster than they can be delivered",
                        (unsigned)n);
      }
    }
  }
}

}  // namespace

bool bus_start() {
  if (s_task) {           // two bus tasks would silently break the promise
    ESP_LOGE("bus", "bus_start() called twice");
    return false;         // that handlers never run in parallel
  }
  return xTaskCreatePinnedToCore(bus_task, "bus", 6144, nullptr, 5, &s_task, 0) == pdPASS;
}

}  // namespace buddy
