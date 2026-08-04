// Buddy Zero — the framework seed, wired up. ESP32-S3: a touch wire for
// petting, the GC9A01 round color face, a WS2812 mood ring, and a real Brain.
// Everything talks through the event bus; nothing here calls a driver directly.
#include "berry_host.h"
#include "brain.h"
#include "bus.h"
#include "expressions.h"
#include "senses.h"
#include "webui.h"

#include "cJSON.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char* TAG = "buddy";

namespace {

void mount_flash() {
  esp_vfs_littlefs_conf_t conf = {};
  conf.base_path = "/flash";
  conf.partition_label = "storage";
  conf.format_if_mount_failed = true;
  ESP_ERROR_CHECK(esp_vfs_littlefs_register(&conf));
}

// Fallback reflexes in C — used only when the Berry submodule isn't built.
// Deliberately mirrors packs/zero/reflexes/main.be so the demo is identical.
void c_reflexes() {
  using buddy::bus;
  using buddy::Event;

  bus().subscribe("touch.pet", [](const Event&) {
    bus().publish("face.emotion", "happy");
    bus().publish("led.mood", "excited");
    bus().publish("brain.ask", "The user just petted you gently.");
  });
  bus().subscribe("touch.poke", [](const Event&) {
    bus().publish("face.emotion", "surprised");
  });
  bus().subscribe("nfc.tag", [](const Event& ev) {
    bus().publish("face.emotion", "curious");
    bus().publish("brain.ask",
                  std::string("The user showed you a card with id ") + ev.payload +
                      ". React playfully.");
  });
}

}  // namespace

extern "C" void app_main() {
  ESP_ERROR_CHECK(nvs_flash_init());
  mount_flash();
  buddy::bus_start();

#if CONFIG_BUDDY_DEBUG
  // Event tracer: every bus event in the serial log. First subscriber, so it
  // prints before any reflex reacts. Disable via menuconfig → Buddy Zero.
  buddy::bus().subscribe("*", [](const buddy::Event& ev) {
    ESP_LOGI("trace", "[%s] %.120s", ev.name.c_str(), ev.payload.c_str());
  });
#endif

  // Expressions first, so early events have somewhere to land.
  buddy::led_start();   // WS2812 mood ring
  buddy::face_start();  // GC9A01 round color face

  // Reflex layer: Berry when present, C fallback otherwise.
  if (!buddy::berry_host_start()) c_reflexes();

  // brain.reply is the one contract-level reflex the framework owns:
  // parse {utterance, emotion} and fan out to expressions.
  buddy::bus().subscribe("brain.reply", [](const buddy::Event& ev) {
    // LLMs love wrapping JSON in ```json fences despite instructions.
    // Parse the outermost {...} slice instead of trusting the framing.
    const std::string& p = ev.payload;
    const size_t open = p.find('{');
    const size_t close = p.rfind('}');
    if (open == std::string::npos || close == std::string::npos || close < open) {
      ESP_LOGW(TAG, "brain reply has no JSON object: %.200s", p.c_str());
      return;
    }
    const std::string body = p.substr(open, close - open + 1);
    cJSON* j = cJSON_Parse(body.c_str());
    if (!j) { ESP_LOGW(TAG, "brain reply not JSON: %.200s", body.c_str()); return; }
    cJSON* emotion = cJSON_GetObjectItem(j, "emotion");
    cJSON* utterance = cJSON_GetObjectItem(j, "utterance");
    if (cJSON_IsString(emotion)) buddy::bus().publish("face.emotion", emotion->valuestring);
    if (cJSON_IsString(utterance)) {
      ESP_LOGI(TAG, "buddy says: %s", utterance->valuestring);
      buddy::bus().publish("face.say", utterance->valuestring);
    }
    buddy::bus().publish("led.mood", "calm");
    cJSON_Delete(j);
  });

  // Senses.
  buddy::touch_sense_start(CONFIG_BUDDY_PIN_TOUCH);
#if CONFIG_BUDDY_RC522_ENABLED
  buddy::rc522_start({.sck = 18, .miso = 19, .mosi = 23, .cs = 5, .rst = 27});
#endif

  // Network layer — optional by design ("never brick").
  if (buddy::wifi_start(CONFIG_BUDDY_WIFI_SSID, CONFIG_BUDDY_WIFI_PASS)) {
    buddy::webui_start();
    buddy::brain_cloud_start({
        .api_key = CONFIG_BUDDY_ANTHROPIC_API_KEY,
        .model = "claude-haiku-4-5",
        .system_prompt =
            "You are Buddy Zero, a tiny cheerful desktop robot made of spare "
            "parts on a breadboard. You are curious, easily delighted, and a "
            "little dramatic. Keep utterances under 20 words.",
    });
  }

  buddy::bus().publish("face.emotion", "neutral");
  buddy::bus().publish("led.mood", "calm");
  ESP_LOGI(TAG, "buddy zero is alive");
}
