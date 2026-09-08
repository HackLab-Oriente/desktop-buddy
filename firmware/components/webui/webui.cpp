#include "webui.h"
#include "bus.h"

#include <cstdio>
#include <cstring>
#include <ctype.h>
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
#include "esp_mac.h"
#include "esp_system.h"
#include "mdns.h"
#include "lwip/sockets.h"

static const char* TAG = "webui";

namespace buddy {
namespace {

EventGroupHandle_t s_wifi_events;
constexpr int kConnected = BIT0;
bool s_ap_mode = false;
char s_ap_ssid[32];

void wifi_handler(void*, esp_event_base_t base, int32_t id, void* data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    if (!s_ap_mode) {
      esp_wifi_connect();
    }
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

constexpr char kSetupPage[] =
    "<!doctype html><meta charset='utf-8'><title>Configuraci&oacute;n de Buddy</title>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<body style='font-family:sans-serif;max-width:400px;margin:2em auto;padding:1em;background:#f5f5f5'>"
    "<div style='background:#fff;padding:2em;border-radius:12px;box-shadow:0 4px 12px rgba(0,0,0,0.1)'>"
    "<h2 style='margin-top:0;color:#333'>Configuraci&oacute;n Wi-Fi</h2>"
    "<p style='color:#666;font-size:0.9em'>Ingresa los datos de tu red para conectar a Buddy.</p>"
    "<form method='POST' action='/setup'>"
    "<div style='margin-bottom:1em'>"
    "<label style='display:block;margin-bottom:0.5em;font-weight:bold'>Nombre de red (SSID)</label>"
    "<input name='ssid' type='text' required style='width:100%;box-sizing:border-box;padding:0.5em;border:1px solid #ccc;border-radius:4px'>"
    "</div>"
    "<div style='margin-bottom:1.5em'>"
    "<label style='display:block;margin-bottom:0.5em;font-weight:bold'>Contrase&ntilde;a</label>"
    "<input name='pass' type='password' style='width:100%;box-sizing:border-box;padding:0.5em;border:1px solid #ccc;border-radius:4px'>"
    "</div>"
    "<div style='margin-bottom:1.5em'>"
    "<label style='display:block;margin-bottom:0.5em;font-weight:bold'>Seguridad</label>"
    "<select name='auth' style='width:100%;box-sizing:border-box;padding:0.5em;border:1px solid #ccc;border-radius:4px'>"
    "<option value='0'>Autom&aacute;tica (WPA/WPA2/Abierta)</option>"
    "<option value='3'>WPA2 PSK</option>"
    "<option value='4'>WPA/WPA2 PSK</option>"
    "<option value='5'>WPA2/WPA3 PSK (Requerido para routers modernos)</option>"
    "</select>"
    "</div>"
    "<button type='submit' style='width:100%;padding:0.75em;background:#0066cc;color:#fff;border:none;border-radius:4px;font-size:1em;cursor:pointer;font-weight:bold'>Conectar</button>"
    "</form>"
    "</div>"
    "</body>";

esp_err_t get_root(httpd_req_t* req) {
  if (s_ap_mode) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/setup");
    return httpd_resp_send(req, nullptr, 0);
  }
  return httpd_resp_send(req, kPage, HTTPD_RESP_USE_STRLEN);
}

esp_err_t get_setup(httpd_req_t* req) {
  return httpd_resp_send(req, kSetupPage, HTTPD_RESP_USE_STRLEN);
}

#include "urldecode.h"
#include "payload_parser.h"

esp_err_t post_setup(httpd_req_t* req) {
  char buf[256] = {0};
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad Request");
  }

  buddy::setup_payload_t payload;
  buddy::parse_setup_payload(buf, &payload);

  if (strlen(payload.ssid) == 0) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing SSID");
  }
  
  ESP_LOGI(TAG, "Captive portal received SSID: '%s' (len: %d)", payload.ssid, strlen(payload.ssid));
  ESP_LOGI(TAG, "Captive portal received Pass len: %d, Auth: %s", strlen(payload.pass), payload.auth);

  wifi_config_t cfg = {};
  strncpy(reinterpret_cast<char*>(cfg.sta.ssid), payload.ssid, sizeof cfg.sta.ssid - 1);
  strncpy(reinterpret_cast<char*>(cfg.sta.password), payload.pass, sizeof cfg.sta.password - 1);
  
  if (strcmp(payload.auth, "3") == 0) {
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  } else if (strcmp(payload.auth, "4") == 0) {
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;
  } else if (strcmp(payload.auth, "5") == 0) {
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_WPA3_PSK;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;
  } else {
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
  }
  
  const char* resp = "<!doctype html><meta charset='utf-8'><title>Guardado</title><body style='font-family:sans-serif;text-align:center;margin-top:2em'><h2>Credenciales guardadas.</h2><p>Buddy se reiniciar&aacute; y se conectar&aacute; a la red.</p></body>";
  httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
  
  vTaskDelay(pdMS_TO_TICKS(1000));
  
  ESP_ERROR_CHECK(esp_wifi_stop());
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
  
  esp_restart();
  return ESP_OK;
}

esp_err_t get_captive_portal(httpd_req_t* req) {
  if (s_ap_mode) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/setup");
    return httpd_resp_send(req, nullptr, 0);
  }
  return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not Found");
}

esp_err_t get_reflex(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/plain");
  FILE* f = fopen("/flash/reflexes/main.be", "r");
  if (!f) return httpd_resp_send(req, "# no script yet\n", HTTPD_RESP_USE_STRLEN);
  char buf[512];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) httpd_resp_send_chunk(req, buf, n);
  fclose(f);
  return httpd_resp_send_chunk(req, nullptr, 0);
}

constexpr size_t kMaxReflexBytes = 64 * 1024;

bool content_type_ok(httpd_req_t* req) {
  char ct[64] = {0};
  if (httpd_req_get_hdr_value_str(req, "Content-Type", ct, sizeof ct) != ESP_OK)
    return false;
  return strncasecmp(ct, "application/json", 16) == 0;
}

esp_err_t post_reflex(httpd_req_t* req) {
  if (!content_type_ok(req))
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "send content-type: application/json");
  if (req->content_len == 0 || req->content_len > kMaxReflexBytes)
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body must be 1..65536 bytes");

  mkdir("/flash/reflexes", 0755);
  FILE* f = fopen("/flash/reflexes/main.be.tmp", "w");
  if (!f) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "fs");

  char buf[512];
  size_t remaining = req->content_len;
  bool ok = true;
  while (remaining > 0) {
    const size_t want = remaining < sizeof buf ? remaining : sizeof buf;
    const int n = httpd_req_recv(req, buf, want);
    if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
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
  return httpd_resp_send(req, "saved — check the log for compile errors", HTTPD_RESP_USE_STRLEN);
}

struct Route {
  const char* uri;
  httpd_method_t method;
  esp_err_t (*fn)(httpd_req_t*);
};

constexpr Route kRoutes[] = {
    {"/", HTTP_GET, get_root},
    {"/setup", HTTP_GET, get_setup},
    {"/setup", HTTP_POST, post_setup},
    {"/reflex", HTTP_GET, get_reflex},
    {"/reflex", HTTP_POST, post_reflex},
    {"/*", HTTP_GET, get_captive_portal}, 
};

static void dns_server_task(void*) {
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (sock < 0) {
    ESP_LOGE(TAG, "DNS socket failed");
    vTaskDelete(nullptr);
    return;
  }
  struct sockaddr_in server_addr = {};
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port = htons(53);
  if (bind(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
    ESP_LOGE(TAG, "DNS bind failed");
    close(sock);
    vTaskDelete(nullptr);
    return;
  }

  char buf[512];
  while (true) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int len = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&client_addr, &client_len);
    if (len < 12) continue; 
    if ((buf[2] & 0x80) != 0) continue; 

    buf[2] |= 0x80; 
    buf[7] = 1;     

    int ptr = 12;
    while (ptr < len && buf[ptr] != 0) {
      ptr += buf[ptr] + 1;
    }
    ptr += 5; 

    if (ptr + 16 > sizeof(buf)) continue;

    buf[ptr++] = 0xC0;
    buf[ptr++] = 0x0C;
    buf[ptr++] = 0x00;
    buf[ptr++] = 0x01;
    buf[ptr++] = 0x00;
    buf[ptr++] = 0x01;
    buf[ptr++] = 0x00;
    buf[ptr++] = 0x00;
    buf[ptr++] = 0x00;
    buf[ptr++] = 0x3C;
    buf[ptr++] = 0x00;
    buf[ptr++] = 0x04;
    buf[ptr++] = 192;
    buf[ptr++] = 168;
    buf[ptr++] = 4;
    buf[ptr++] = 1;

    sendto(sock, buf, ptr, 0, (struct sockaddr*)&client_addr, client_len);
  }
}

}  // namespace

bool wifi_start(const char* ssid, const char* pass) {
  s_wifi_events = xEventGroupCreate();
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_t* sta_netif = esp_netif_create_default_wifi_sta();
  esp_netif_t* ap_netif = esp_netif_create_default_wifi_ap();
  (void)sta_netif; // Suppress unused variable warning
  (void)ap_netif;
  
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init));
  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler, nullptr);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_handler, nullptr);

  wifi_config_t nvs_cfg = {};
  esp_wifi_get_config(WIFI_IF_STA, &nvs_cfg);
  
  bool has_creds = (ssid && ssid[0]) || nvs_cfg.sta.ssid[0];

  if (has_creds) {
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    if (ssid && ssid[0]) {
      wifi_config_t cfg = {};
      strlcpy(reinterpret_cast<char*>(cfg.sta.ssid), ssid, sizeof(cfg.sta.ssid));
      strlcpy(reinterpret_cast<char*>(cfg.sta.password), pass, sizeof(cfg.sta.password));
      ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    
    const bool connected = xEventGroupWaitBits(s_wifi_events, kConnected, pdFALSE, pdTRUE,
                                               pdMS_TO_TICKS(15000)) & kConnected;
    if (connected) {
      esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
      esp_netif_sntp_init(&sntp_cfg);
      bus().publish("boot.status", "sincronizando hora");
      if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)) == ESP_OK) {
        time_t now = time(nullptr);
        ESP_LOGI(TAG, "time synced: %s", ctime(&now));
        bus().publish("time.synced");
      }
      return true;
    } else {
      ESP_LOGW(TAG, "Failed to connect to STA. Falling back to AP mode.");
    }
  }

  s_ap_mode = true;
  // If it was started, stop it before changing mode
  if (has_creds) {
    ESP_ERROR_CHECK(esp_wifi_stop());
  }
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    char ssid_ap[32];
    snprintf(ssid_ap, sizeof(ssid_ap), "Hacklab-Buddy");
    
    wifi_config_t ap_cfg = {};
    strlcpy(reinterpret_cast<char*>(ap_cfg.ap.ssid), ssid_ap, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = strlen(ssid_ap);
    ap_cfg.ap.channel = 1;
    strlcpy(reinterpret_cast<char*>(ap_cfg.ap.password), "buddy123", sizeof(ap_cfg.ap.password));
    ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.pmf_cfg.required = false;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
  
  ESP_ERROR_CHECK(esp_wifi_start());
  
  mdns_init();
  mdns_hostname_set("config");
  mdns_instance_name_set("Buddy Zero");
  
  xTaskCreate(dns_server_task, "dns_server", 4096, nullptr, 5, nullptr);
  
  char qr_payload[128];
  snprintf(qr_payload, sizeof(qr_payload), "WIFI:T:WPA;S:%s;P:buddy123;;", ssid_ap);
  bus().publish("wifi.ap_mode", qr_payload);
  
  return false;
}

bool webui_start() {
  httpd_handle_t server = nullptr;
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
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
