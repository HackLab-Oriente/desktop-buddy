#include "brain.h"
#include "bus.h"

#include <cstring>
#include <string>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char* TAG = "brain";

namespace buddy {
namespace {

BrainConfig s_cfg;
QueueHandle_t s_asks;  // queue of char* (heap-owned), so the bus task never blocks on TLS

// Persistent HTTP client: on the classic ESP32 the TLS handshake is 3-6 s of
// software ECC, so we keep the connection alive across requests and only pay
// that price on the first ask (or after the server drops us).
esp_http_client_handle_t s_client = nullptr;

esp_http_client_handle_t ensure_client() {
  if (s_client) return s_client;
  esp_http_client_config_t http = {};
  http.url = "https://api.anthropic.com/v1/messages";
  http.method = HTTP_METHOD_POST;
  http.crt_bundle_attach = esp_crt_bundle_attach;
  http.timeout_ms = 30000;
  http.buffer_size = 4096;
  http.keep_alive_enable = true;
  s_client = esp_http_client_init(&http);
  if (s_client) {
    esp_http_client_set_header(s_client, "content-type", "application/json");
    esp_http_client_set_header(s_client, "x-api-key", s_cfg.api_key);
    esp_http_client_set_header(s_client, "anthropic-version", "2023-06-01");
  }
  return s_client;
}

void drop_client() {
  if (s_client) esp_http_client_cleanup(s_client);
  s_client = nullptr;
}

// Ask Claude for a reply as strict JSON {utterance, emotion}. Non-streaming
// for the PoC; v1 streams so the face reacts as tokens arrive.
std::string call_claude(const char* user_text) {
  cJSON* root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "model", s_cfg.model);
  cJSON_AddNumberToObject(root, "max_tokens", 300);
  std::string system = std::string(s_cfg.system_prompt) +
      "\nRespond with ONLY a raw JSON object — no markdown, no code fences, no"
      " text before or after: {\"utterance\": the SPOKEN WORDS ONLY, under 90"
      " characters, in character. Do NOT include stage directions, actions, or"
      " sound effects in asterisks or parentheses (e.g. never write '*whirrs*'"
      " or '(giggles)') — just what the buddy says out loud."
      " \"emotion\": one of neutral|happy|curious|sleepy|surprised|angry|sad|suspicious}";
  cJSON_AddStringToObject(root, "system", system.c_str());
  cJSON* msgs = cJSON_AddArrayToObject(root, "messages");
  cJSON* m = cJSON_CreateObject();
  cJSON_AddStringToObject(m, "role", "user");
  cJSON_AddStringToObject(m, "content", user_text);
  cJSON_AddItemToArray(msgs, m);
  char* body = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);

#if CONFIG_BUDDY_DEBUG
  ESP_LOGI(TAG, "POST /v1/messages model=%s body=%d bytes", s_cfg.model, (int)strlen(body));
  ESP_LOGI(TAG, "request: %s", body);
#endif

  std::string reply;
  // Two attempts: a stale kept-alive connection fails fast on the first
  // open/write, then we reconnect fresh (paying the handshake) and retry.
  for (int attempt = 0; attempt < 2 && reply.empty(); attempt++) {
    esp_http_client_handle_t client = ensure_client();
    if (!client) break;
    esp_err_t err = esp_http_client_open(client, strlen(body));
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "open failed (%s), %s", esp_err_to_name(err),
               attempt == 0 ? "reconnecting" : "giving up");
      drop_client();
      continue;
    }
    if (esp_http_client_write(client, body, strlen(body)) < 0 ||
        esp_http_client_fetch_headers(client) < 0) {
      ESP_LOGW(TAG, "write/headers failed on kept-alive connection, %s",
               attempt == 0 ? "reconnecting" : "giving up");
      drop_client();
      continue;
    }
    char buf[512];
    int n;
    while ((n = esp_http_client_read(client, buf, sizeof buf)) > 0)
      reply.append(buf, n);
    const int status = esp_http_client_get_status_code(client);
    if (status != 200) {
      ESP_LOGE(TAG, "HTTP %d: %.256s", status, reply.c_str());
      break;  // a real API answer (401, 429, …) — retrying won't help
    }
#if CONFIG_BUDDY_DEBUG
    ESP_LOGI(TAG, "HTTP 200, %d bytes: %.256s", (int)reply.size(), reply.c_str());
#endif
  }
  cJSON_free(body);
  return reply;
}

void brain_task(void*) {
  char* ask;
  for (;;) {
    if (xQueueReceive(s_asks, &ask, portMAX_DELAY) != pdTRUE) continue;
    bus().publish("led.mood", "thinking");
    std::string raw = call_claude(ask);
    free(ask);

    // API response → content[0].text, which is itself our JSON contract.
    cJSON* resp = cJSON_Parse(raw.c_str());
    cJSON* content = resp ? cJSON_GetObjectItem(resp, "content") : nullptr;
    cJSON* first = content ? cJSON_GetArrayItem(content, 0) : nullptr;
    cJSON* text = first ? cJSON_GetObjectItem(first, "text") : nullptr;
    if (text && cJSON_IsString(text)) {
      bus().publish("brain.reply", text->valuestring);
    } else {
      ESP_LOGE(TAG, "bad response: %.200s", raw.c_str());
      bus().publish("brain.error", "no_reply");
    }
    if (resp) cJSON_Delete(resp);
  }
}

}  // namespace

void brain_cloud_start(const BrainConfig& cfg) {
  s_cfg = cfg;
  if (!cfg.api_key || !cfg.api_key[0]) {
    ESP_LOGW(TAG, "no API key configured — brain.ask will be ignored");
    return;
  }
  s_asks = xQueueCreate(4, sizeof(char*));
  bus().subscribe("brain.ask", [](const Event& ev) {
    char* copy = strdup(ev.payload.c_str());
    if (xQueueSend(s_asks, &copy, 0) != pdTRUE) free(copy);  // brain busy, drop
  });
  xTaskCreatePinnedToCore(brain_task, "brain", 10240, nullptr, 4, nullptr, 0);
}

}  // namespace buddy
