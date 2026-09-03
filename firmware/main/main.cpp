// Buddy Zero — the framework seed, wired up. ESP32-S3: a touch wire for
// petting, the GC9A01 round color face, a WS2812 mood ring, and a real Brain.
// Everything talks through the event bus; nothing here calls a driver directly.
//
// Consumes: brain.reply  (the {emotion, utterance} contract, fanned out)
//           speech.say   (routed to face.say until voice subscribes too)
// Emits:    boot.status · boot.ready · face.emotion · face.say · led.mood
//
// FOUR RULES GOVERN THE ORDER BELOW. Three of them are invisible in the code:
//
//   1. Subscribers before publishers. The bus has no replay — an event
//      published before its subscriber exists is gone, not delivered late.
//   2. The face first, because everything after it is slow. wifi_start alone
//      is up to 25 s, and the splash is what covers it.
//   3. Flash before reflexes: the reflex layer reads /flash.
//   4. Reflexes before senses. Start touch first and the first pet does
//      nothing — the failure a workshop attendee cannot diagnose.
#include "berry_host.h"
#include "brain.h"
#include "bus.h"
#include "expressions.h"
#include "face_model.h"
#include "pack.h"
#include "senses.h"
#include "webui.h"

#include "cJSON.h"
#include "esp_err.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char* TAG = "buddy";

namespace {

// Deliberately NOT ESP_ERROR_CHECK. This now runs before face_start(), so an
// abort here is a black screen and a silent boot loop -- the buddy would have
// no way to say what happened. A missing or corrupt storage partition costs
// the pack, not the creature: without it the built-in face and moods are
// still there, which is the whole point of keeping built-ins.
void mount_flash() {
  esp_vfs_littlefs_conf_t conf = {};
  conf.base_path = "/flash";
  conf.partition_label = "storage";
  conf.format_if_mount_failed = true;
  const esp_err_t err = esp_vfs_littlefs_register(&conf);
  if (err != ESP_OK)
    ESP_LOGE(TAG, "no /flash (%s) — built-in face, no pack and no reflexes",
             esp_err_to_name(err));
}


}  // namespace

extern "C" void app_main() {
  // Not ESP_ERROR_CHECK. NO_FREE_PAGES and NEW_VERSION_FOUND are ordinary
  // outcomes after an OTA or a partition-table change, and aborting here is a
  // dark screen and a reboot loop with the reason only on a serial console
  // nobody at a workshop has attached.
  esp_err_t nvs = nvs_flash_init();
  if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "nvs needs erasing (%s)", esp_err_to_name(nvs));
    if (nvs_flash_erase() == ESP_OK) nvs = nvs_flash_init();
  }
  if (nvs != ESP_OK) ESP_LOGE(TAG, "no nvs (%s) — wifi will not start",
                              esp_err_to_name(nvs));
  buddy::bus_start();

#if CONFIG_BUDDY_DEBUG
  // Event tracer: every bus event in the serial log. First subscriber, so it
  // prints before any reflex reacts. Disable via menuconfig → Buddy Zero.
  buddy::bus().subscribe("*", [](const buddy::Event& ev) {
    ESP_LOGI("trace", "[%s] %.120s", ev.name.c_str(), ev.payload.c_str());
  });
#endif

  // Flash and the pack come before the face, and that ordering is not
  // cosmetic. The pack REPLACES the expression and mood tables, and the render
  // task reads those tables every frame — swapping them under a running
  // renderer is a use-after-free, not a style question. Both are fast because
  // the build flashes a prepared LittleFS image; only a board flashed without
  // one pays for a format here.
  mount_flash();
  buddy::pack_load();

  // Now the face, so the boot splash is on screen while everything slower
  // happens behind it. Each step below announces itself on the bus, and the
  // splash shows that as its status line — a real step, not a fake timer.
  buddy::face_start();  // GC9A01 round color face + boot splash
  buddy::led_start();   // WS2812 mood ring

  // Reflex layer: Berry when present, C fallback otherwise.
  buddy::bus().publish("boot.status", "loading reflexes");
  // No C fallback. It was a second copy of the seed behaviour that had
  // already drifted from packs/zero, and its trigger is a build mistake — a
  // clone without --recursive — not a device state. "Never a brick" protects
  // the creature from its environment; it does not owe anyone a quieter,
  // wronger demo that looks like the real one.
  if (!buddy::berry_host_start())
    buddy::bus().publish("boot.status", "sin reflejos");

  // brain.reply is the one contract-level reflex the framework owns:
  // parse {utterance, emotion} and fan out to expressions.
  buddy::bus().subscribe("brain.reply", [](const buddy::Event& ev) {
    // LLMs wrap JSON in fences despite instructions, and describe the format
    // in prose before emitting it. Anchoring on the FIRST '{' broke on
    // "the format {utterance, emotion}: {...}" — an ordinary reply — so every
    // '{' is tried in turn until one parses. cJSON tolerates trailing text.
    const std::string& p = ev.payload;
    cJSON* j = nullptr;
    for (size_t at = p.find('{'); at != std::string::npos && !j;
         at = p.find('{', at + 1))
      j = cJSON_Parse(p.c_str() + at);
    if (!j) { ESP_LOGW(TAG, "brain reply has no JSON object: %.200s", p.c_str()); return; }
    cJSON* emotion = cJSON_GetObjectItem(j, "emotion");
    cJSON* utterance = cJSON_GetObjectItem(j, "utterance");
    // Validated here, not downstream: the registry documents face.emotion as
    // a name from the table, and packs subscribe to the bus. A contract has to
    // hold at the publisher.
    if (cJSON_IsString(emotion) && buddy::emotion_index(emotion->valuestring) >= 0)
      buddy::bus().publish("face.emotion", emotion->valuestring);
    else if (cJSON_IsString(emotion))
      ESP_LOGW(TAG, "brain returned an unknown emotion: %.32s", emotion->valuestring);
    if (cJSON_IsString(utterance)) {
      ESP_LOGI(TAG, "buddy says: %.120s", utterance->valuestring);
      buddy::bus().publish("face.say", utterance->valuestring);
    }
    cJSON_Delete(j);
  });

  // speech.say is "these exact words come out", through every channel the
  // buddy has. Today that is the screen; when voice lands, TTS subscribes here
  // too and no reflex changes. face.say stays what it always was — screen
  // only, which is what buddy.hint() publishes.
  buddy::bus().subscribe("speech.say", [](const buddy::Event& ev) {
    buddy::bus().publish("face.say", ev.payload);
  });

  // Senses.
  buddy::bus().publish("boot.status", "waking senses");
  buddy::touch_start(CONFIG_BUDDY_PIN_TOUCH);
#if CONFIG_BUDDY_RC522_ENABLED
  buddy::nfc_start({.sck = CONFIG_BUDDY_RC522_SCK,
                      .miso = CONFIG_BUDDY_RC522_MISO,
                      .mosi = CONFIG_BUDDY_RC522_MOSI,
                      .cs = CONFIG_BUDDY_RC522_CS,
                      .rst = CONFIG_BUDDY_RC522_RST});
#endif

  // Network layer — optional by design ("never brick"). This is the slow part:
  // Up to 25 s: 15 waiting for the association, then 10 more for SNTP. That
  // second one is the common case on guest wifi, where UDP/123 is blocked.
  buddy::bus().publish("boot.status", "conectando wifi");
  const bool online = buddy::wifi_start(CONFIG_BUDDY_WIFI_SSID, CONFIG_BUDDY_WIFI_PASS);

  // Outside the branch on purpose: the provisioning portal is FOR the buddy
  // that could not join, so putting its door behind "did we join" made it
  // unreachable exactly when it is needed.
  buddy::webui_start();

  if (online) {
    buddy::bus().publish("boot.status", "waking brain");
    buddy::brain_start({
        .api_key = CONFIG_BUDDY_ANTHROPIC_API_KEY,
        .model = "claude-haiku-4-5",
        .system_prompt =
            "You are Buddy Zero, a tiny cheerful desktop robot made of spare "
            "parts on a breadboard. You are curious, easily delighted, and a "
            "little dramatic. Keep utterances under 20 words.",
    });
  } else {
    buddy::bus().publish("boot.status", "offline");
    // Still start it: with no network it answers every ask with brain.error
    // "no_key"/"offline" instead of swallowing them, so a pack can tell
    // "thinking" from "there is no brain".
    buddy::brain_start({.api_key = "", .model = "", .system_prompt = ""});
  }

  buddy::bus().publish("face.emotion", "neutral");
  buddy::bus().publish("led.mood", "calm");
  // Tells the face to glitch out of the splash and become a creature.
  buddy::bus().publish("boot.ready");
  ESP_LOGI(TAG, "buddy zero is alive");
}
