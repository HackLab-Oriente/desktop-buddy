#include "webui.h"
#include "bus.h"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

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
    "<button onclick=\"fetch('/reflex',{method:'POST',"
    "headers:{'content-type':'application/json'},body:s.value})"
    ".then(r=>r.text()).then(t=>alert(t))\">Upload &amp; hot-reload</button>"
    "<script>fetch('/reflex').then(r=>r.text()).then(t=>s.value=t)</script>";

esp_err_t get_root(httpd_req_t* req) {
  return httpd_resp_send(req, kPage, HTTPD_RESP_USE_STRLEN);
}

esp_err_t get_reflex(httpd_req_t* req) {
  // ESP-IDF's default response type is text/html, so a reflex containing a
  // <script> tag executes in the browser of whoever opens this URL.
  httpd_resp_set_type(req, "text/plain");
  FILE* f = fopen("/flash/reflexes/main.be", "r");
  if (!f) return httpd_resp_send(req, "# no script yet\n", HTTPD_RESP_USE_STRLEN);
  char buf[512];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) httpd_resp_send_chunk(req, buf, n);
  fclose(f);
  return httpd_resp_send_chunk(req, nullptr, 0);
}

// A reflex is a few kilobytes; anything near this is a mistake or an attempt
// to fill the pack partition, which is 2.06 MB on the classic board.
constexpr size_t kMaxReflexBytes = 64 * 1024;

// text/plain is on the CORS safelist, so a POST carrying it is a "simple
// request" and any page the owner visits can write here with no preflight and
// without reading the reply. Requiring a type that is NOT safelisted forces a
// preflight the device never answers.
bool content_type_ok(httpd_req_t* req) {
  char ct[64] = {0};
  if (httpd_req_get_hdr_value_str(req, "Content-Type", ct, sizeof ct) != ESP_OK)
    return false;
  return strncasecmp(ct, "application/json", 16) == 0;
}

esp_err_t post_reflex(httpd_req_t* req) {
  if (!content_type_ok(req))
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                               "send content-type: application/json");
  // content_len is size_t and 0 for chunked transfers. Refusing both up front
  // is what keeps the truncation below from ever being reachable.
  if (req->content_len == 0 || req->content_len > kMaxReflexBytes)
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body must be 1..65536 bytes");

  mkdir("/flash/reflexes", 0755);
  // Written beside the real file and renamed only on success. Opening the
  // real one with "w" truncated it before a single byte arrived, so any
  // interrupted upload -- or a Content-Length the size_t conversion turned
  // negative -- left the buddy with no reflexes at all, reloaded it, and
  // answered "ok".
  FILE* f = fopen("/flash/reflexes/main.be.tmp", "w");
  if (!f) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "fs");

  char buf[512];
  size_t remaining = req->content_len;
  bool ok = true;
  while (remaining > 0) {
    const size_t want = remaining < sizeof buf ? remaining : sizeof buf;
    const int n = httpd_req_recv(req, buf, want);
    if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;   // a pause is not an ending
    if (n <= 0) { ok = false; break; }
    if (fwrite(buf, 1, static_cast<size_t>(n), f) != static_cast<size_t>(n)) {
      ok = false;
      break;
    }
    remaining -= static_cast<size_t>(n);
  }
  if (fclose(f) != 0) ok = false;

  if (!ok || remaining != 0) {
    unlink("/flash/reflexes/main.be.tmp");
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "upload incomplete");
  }
  if (rename("/flash/reflexes/main.be.tmp", "/flash/reflexes/main.be") != 0) {
    unlink("/flash/reflexes/main.be.tmp");
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rename failed");
  }
  bus().publish("system.reload");
  return httpd_resp_send(req, "saved — check the log for compile errors",
                         HTTPD_RESP_USE_STRLEN);
}

struct Route {
  const char* uri;
  httpd_method_t method;
  esp_err_t (*fn)(httpd_req_t*);
};

constexpr Route kRoutes[] = {
    {"/", HTTP_GET, get_root},
    {"/reflex", HTTP_GET, get_reflex},
    {"/reflex", HTTP_POST, post_reflex},
};

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

bool webui_start() {
  httpd_handle_t server = nullptr;
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  // Without this, seven idle sockets take every slot and never yield one.
  // lwIP has ten in total, and SNTP holds one for the life of the device, so
  // the brain loses its TLS socket too.
  cfg.lru_purge_enable = true;
  cfg.max_uri_handlers = sizeof kRoutes / sizeof *kRoutes;
  cfg.uri_match_fn = httpd_uri_match_wildcard;

  const esp_err_t err = httpd_start(&server, &cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "no web ui: %s", esp_err_to_name(err));
    return false;
  }
  for (const Route& r : kRoutes) {
    const httpd_uri_t u = {r.uri, r.method, r.fn, nullptr};
    const esp_err_t e = httpd_register_uri_handler(server, &u);
    if (e != ESP_OK) ESP_LOGE(TAG, "route %s: %s", r.uri, esp_err_to_name(e));
  }
  ESP_LOGI(TAG, "web ui up — open http://<device-ip>/");
  return true;
}

}  // namespace buddy
