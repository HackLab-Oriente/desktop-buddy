// Buddy Zero — the framework seed, wired up. ESP32-S3: a touch wire for
// petting, the GC9A01 round color face, a WS2812 mood ring, and a real Brain.
// Everything talks through the event bus; nothing here calls a driver directly.
#include "berry_host.h"
#include "markov.h"
#include "voice.h"
#include "brain.h"
#include "bus.h"
#include "expressions.h"
#include "senses.h"
#include "webui.h"

#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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

// El botón de PTT del spike de voz (GPIO 5, a masa, pull-up interno).
// Publica un HECHO (`button.ptt`), no una orden: quién reacciona y cómo lo
// deciden los reflejos. Ver docs/event-registry.md, convención 2.
constexpr gpio_num_t kPttPin = GPIO_NUM_5;

void ptt_task(void*) {
  gpio_config_t io = {};
  io.pin_bit_mask = 1ULL << kPttPin;
  io.mode = GPIO_MODE_INPUT;
  io.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&io);
  bool was_down = false;
  while (true) {
    const bool down = gpio_get_level(kPttPin) == 0;   // pull-up: bajo = pulsado
    if (down && !was_down) {
      buddy::bus().publish("button.ptt", "down");
      vTaskDelay(pdMS_TO_TICKS(40));                  // antirrebote
    }
    was_down = down;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

}  // namespace

extern "C" void app_main() {
  ESP_ERROR_CHECK(nvs_flash_init());
  buddy::bus_start();

#if CONFIG_BUDDY_DEBUG
  // Event tracer: every bus event in the serial log. First subscriber, so it
  // prints before any reflex reacts. Disable via menuconfig → Buddy Zero.
  buddy::bus().subscribe("*", [](const buddy::Event& ev) {
    ESP_LOGI("trace", "[%s] %.120s", ev.name.c_str(), ev.payload.c_str());
  });
#endif

  // The face comes up first so the boot splash is on screen while everything
  // slower happens behind it. Each step below announces itself on the bus, and
  // the splash shows that as its status line — a real step, not a fake timer.
  buddy::face_start();  // GC9A01 round color face + boot splash
  buddy::led_start();   // WS2812 mood ring

  buddy::bus().publish("boot.status", "mounting packs");
  mount_flash();

  // Reflex layer: Berry when present, C fallback otherwise.
  buddy::bus().publish("boot.status", "loading reflexes");
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

  // Frases locales: la cadena de Markov sobre los bancos por registro.
  // Nunca es motivo de no arrancar — si falla, el botón simplemente no dirá
  // nada y todo lo demás sigue igual.
  buddy::bus().publish("boot.status", "cargando frases");
  if (markov_start()) {
    buddy::bus().subscribe("button.ptt", [](const buddy::Event&) {
      // Registro al azar, para que la demo enseñe los siete. En un pack de
      // verdad esto sale del mapa expresión -> registro, no de un sorteo.
      static int n = 0;
      const char* reg = markov_register(n++ % 7);
      char line[160];
      const int64_t t0 = esp_timer_get_time();
      const int len = markov_say(reg, line, sizeof line);
      const int64_t us = esp_timer_get_time() - t0;
      if (len > 0) {
        ESP_LOGI(TAG, "[%s] %s  (%lld us)", reg, line, us);
        buddy::bus().publish("face.say", line);
      }
    });
  }

  // La voz se engancha a `face.say`, no a Markov: así se lee en alto TODO lo
  // que el bicho dice — las frases locales ahora y las del cerebro cloud
  // cuando las haya — y ninguna de las dos fuentes sabe que la voz existe.
  buddy::bus().publish("boot.status", "afinando la voz");
  if (voice_start()) {
    buddy::bus().subscribe("face.say", [](const buddy::Event& ev) {
      voice_say(ev.payload.c_str());     // solo encola; el bus nunca se bloquea
    });
  }

  // Senses.
  buddy::bus().publish("boot.status", "waking senses");
  xTaskCreate(ptt_task, "ptt", 2560, nullptr, 4, nullptr);
  buddy::touch_sense_start(CONFIG_BUDDY_PIN_TOUCH);
#if CONFIG_BUDDY_RC522_ENABLED
  buddy::rc522_start({.sck = CONFIG_BUDDY_RC522_SCK,
                      .miso = CONFIG_BUDDY_RC522_MISO,
                      .mosi = CONFIG_BUDDY_RC522_MOSI,
                      .cs = CONFIG_BUDDY_RC522_CS,
                      .rst = CONFIG_BUDDY_RC522_RST});
#endif

  // Network layer — optional by design ("never brick"). This is the slow part:
  // wifi_start blocks for up to 15 s, which is exactly why the splash exists.
  buddy::bus().publish("boot.status", "connecting wifi");
  if (buddy::wifi_start(CONFIG_BUDDY_WIFI_SSID, CONFIG_BUDDY_WIFI_PASS)) {
    buddy::bus().publish("boot.status", "waking brain");
    buddy::webui_start();
    buddy::brain_cloud_start({
        .api_key = CONFIG_BUDDY_ANTHROPIC_API_KEY,
        .model = "claude-haiku-4-5",
        .system_prompt =
            "You are Buddy Zero, a tiny cheerful desktop robot made of spare "
            "parts on a breadboard. You are curious, easily delighted, and a "
            "little dramatic. Keep utterances under 20 words.",
    });
  } else {
    buddy::bus().publish("boot.status", "offline");
  }

  buddy::bus().publish("face.emotion", "neutral");
  buddy::bus().publish("led.mood", "calm");
  // Tells the face to glitch out of the splash and become a creature.
  buddy::bus().publish("boot.ready");
  ESP_LOGI(TAG, "buddy zero is alive");
}
