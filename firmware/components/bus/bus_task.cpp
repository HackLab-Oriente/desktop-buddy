// ESP-side dispatch loop: one task owns pump(), so all handlers run in a
// single context and never need their own locking.
#include "bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace buddy {

static void bus_task(void*) {
  for (;;) {
    if (bus().pump() == 0) vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void bus_start() {
  xTaskCreatePinnedToCore(bus_task, "bus", 6144, nullptr, 5, nullptr, 0);
}

}  // namespace buddy
