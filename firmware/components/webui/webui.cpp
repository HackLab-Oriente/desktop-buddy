#include "webui.h"
#include "bus.h"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#include <ctime>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

static const char* TAG = "webui";

namespace buddy {
namespace {

EventGroupHandle_t s_wifi_events;
constexpr int kConnected = BIT0;

void wifi_handler(void*, esp_event_base_t base, int32_t id, void* data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    esp_wifi_connect();
    xEventGroupClearBits(s_wifi_events, kConnected);
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    auto* ev = static_cast<ip_event_got_ip_t*>(data);
    ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&ev->ip_info.ip));
    xEventGroupSetBits(s_wifi_events, kConnected);
  }
}

constexpr char kPage[] =
    "<!doctype html><title>Buddy Zero</title>"
    "<body style='font-family:monospace;max-width:640px;margin:2em auto'>"
    "<h2>Buddy Zero — reflex editor</h2>"
    "<textarea id=s rows=24 style='width:100%'></textarea><br>"
    "<button onclick=\"fetch('/reflex',{method:'POST',body:s.value})"
    ".then(()=>alert('reloaded'))\">Upload &amp; hot-reload</button>"
    "<script>fetch('/reflex').then(r=>r.text()).then(t=>s.value=t)</script>";

esp_err_t get_root(httpd_req_t* req) {
  return httpd_resp_send(req, kPage, HTTPD_RESP_USE_STRLEN);
}

esp_err_t get_reflex(httpd_req_t* req) {
  FILE* f = fopen("/flash/reflexes/main.be", "r");
  if (!f) return httpd_resp_send(req, "# no script yet\n", HTTPD_RESP_USE_STRLEN);
  char buf[512];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) httpd_resp_send_chunk(req, buf, n);
  fclose(f);
  return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t post_reflex(httpd_req_t* req) {
  mkdir("/flash/reflexes", 0755);
  FILE* f = fopen("/flash/reflexes/main.be", "w");
  if (!f) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "fs");
  char buf[512];
  int remaining = req->content_len;
  while (remaining > 0) {
    int n = httpd_req_recv(req, buf, remaining < (int)sizeof buf ? remaining : sizeof buf);
    if (n <= 0) break;
    fwrite(buf, 1, n, f);
    remaining -= n;
  }
  fclose(f);
  bus().publish("system.reload");
  return httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
}

}  // namespace

bool wifi_start(const char* ssid, const char* pass) {
  if (!ssid || !ssid[0]) {
    ESP_LOGW(TAG, "no WiFi configured — offline mode (reflexes still run)");
    return false;
  }
  s_wifi_events = xEventGroupCreate();
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init));
  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler, nullptr);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_handler, nullptr);

  wifi_config_t cfg = {};
  strncpy(reinterpret_cast<char*>(cfg.sta.ssid), ssid, sizeof cfg.sta.ssid - 1);
  strncpy(reinterpret_cast<char*>(cfg.sta.password), pass, sizeof cfg.sta.password - 1);
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
  ESP_ERROR_CHECK(esp_wifi_start());

  const bool connected = xEventGroupWaitBits(s_wifi_events, kConnected, pdFALSE, pdTRUE,
                                             pdMS_TO_TICKS(15000)) & kConnected;
  if (connected) {
    // TLS certificate validation needs real time — the chip boots in 1970.
    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sntp_cfg);
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)) == ESP_OK) {
      time_t now = time(nullptr);
      ESP_LOGI(TAG, "time synced: %s", ctime(&now));
      bus().publish("time.synced");
    } else {
      ESP_LOGW(TAG, "SNTP sync timed out — TLS may reject certificates");
    }
  }
  return connected;
}

void webui_start() {
  httpd_handle_t server = nullptr;
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  if (httpd_start(&server, &cfg) != ESP_OK) return;
  static const httpd_uri_t root = {"/", HTTP_GET, get_root, nullptr};
  static const httpd_uri_t r_get = {"/reflex", HTTP_GET, get_reflex, nullptr};
  static const httpd_uri_t r_post = {"/reflex", HTTP_POST, post_reflex, nullptr};
  httpd_register_uri_handler(server, &root);
  httpd_register_uri_handler(server, &r_get);
  httpd_register_uri_handler(server, &r_post);
  ESP_LOGI(TAG, "web ui up — open http://<device-ip>/");
}

}  // namespace buddy
