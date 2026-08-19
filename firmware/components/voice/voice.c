// Voz: convierte una frase en audio con la API de OpenAI y la saca por el
// MAX98357A.
//
// Dos decisiones que quitan trabajo:
//
// 1. **Se pide `wav`, no `mp3`.** Un WAV ya es PCM: leer la cabecera y a
//    reproducir. El decodificador que todo el mundo da por obligatorio se
//    evita pidiendo el formato correcto. El tamaño no es problema con 8 MB de
//    PSRAM.
// 2. **Todo ocurre en una tarea propia.** Un manejador del bus NUNCA puede
//    bloquear (convención 4 del registro de eventos): la petición HTTPS tarda
//    segundos y pararía la cara, los reflejos y todo lo demás. `voice_say()`
//    solo encola.
//
// La clave sale de Kconfig y no se escribe en ningún log. Si está vacía, el
// componente se queda dormido y el resto del firmware funciona igual.
#include "voice.h"

#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/netdb.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define TAG "voice"
#define API "https://api.openai.com/v1/audio/speech"

// Una frase del buddy son 2-4 s. A 24 kHz mono 16 bits, 4 s son 192 KB; el
// techo deja sitio de sobra sin permitir que una respuesta rara se coma la
// PSRAM entera.
#define MAX_WAV (600 * 1024)
#define TEXT_MAX 200

static i2s_chan_handle_t s_tx;
static QueueHandle_t s_q;
static uint8_t *s_buf;
static int s_rate_open;                 // a qué frecuencia está abierto el I2S

static void amp_enabled(bool on) { gpio_set_level(CONFIG_BUDDY_TTS_PIN_MUTE, on ? 1 : 0); }

// El WAV de OpenAI viene a 24 kHz; aun así se lee de la cabecera, porque dar
// por supuesta la frecuencia es la forma clásica de oír a alguien hablar como
// una ardilla.
static void i2s_open(int rate) {
    if (s_tx && s_rate_open == rate) return;
    if (s_tx) { i2s_channel_disable(s_tx); i2s_del_channel(s_tx); s_tx = NULL; }
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&cc, &s_tx, NULL));
    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED,
                     .bclk = CONFIG_BUDDY_TTS_PIN_BCLK,
                     .ws = CONFIG_BUDDY_TTS_PIN_WS,
                     .dout = CONFIG_BUDDY_TTS_PIN_DOUT,
                     .din = I2S_GPIO_UNUSED},
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx, &cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx));
    s_rate_open = rate;
}

// --- una frase en vuelo ----------------------------------------------------
// El audio suena MIENTRAS se descarga. Antes se esperaba al último byte y solo
// entonces sonaba algo; ahora el primer sonido sale en cuanto hay un pelín de
// margen, y la descarga deja de contar en la espera.
typedef struct {
    uint8_t hdr[256];
    int  hdr_len;
    bool parsed;
    int  rate, ch;
    bool playing;
    int  staged;                 // PCM guardado en s_buf antes de arrancar
    int  total_pcm;
    uint8_t carry;               // un chunk puede partir una muestra en dos
    bool has_carry;
    int64_t t_first_sound;
} Stream;

// ~0,25 s de colchón antes de arrancar: sin él, un hipo de red se oye como un
// corte; con demasiado, se pierde la ventaja de streamear.
#define PREBUFFER_BYTES (24000 * 2 / 4)

static esp_http_client_handle_t s_cli;   // se reutiliza entre frases
static Stream s_st;

// Duplica mono a los dos canales; el MAX98357A está cableado en estéreo.
// Bloquea al ritmo del DMA, que es justo lo que regula la descarga.
static void feed_i2s(const uint8_t *pcm, int bytes, int channels) {
    enum { FRAMES = 512 };
    static int16_t out[FRAMES * 2];
    const int16_t *in = (const int16_t *)pcm;
    int samples = bytes / 2;
    for (int i = 0; i < samples;) {
        int n = 0;
        while (n < FRAMES && i < samples) {
            int16_t l = in[i++];
            int16_t r = (channels == 2 && i < samples) ? in[i++] : l;
            out[n * 2] = l; out[n * 2 + 1] = r;
            n++;
        }
        size_t wrote = 0;
        i2s_channel_write(s_tx, out, n * 2 * sizeof(int16_t), &wrote, 2000);
    }
}

// Recorre los trozos RIFF hasta `data`. Devuelve el desplazamiento del PCM o
// -1 si todavía no hay cabecera suficiente. El tamaño de `data` no se mira: en
// streaming es relleno (0 o 0xFFFFFFFF), y hacerle caso costó una tarde.
static int parse_riff(const uint8_t *b, int len, int *rate, int *ch) {
    if (len < 12 || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) return -1;
    for (int off = 12; off + 8 <= len;) {
        const uint8_t *c = b + off;
        uint32_t sz = c[4] | c[5] << 8 | c[6] << 16 | (uint32_t)c[7] << 24;
        if (!memcmp(c, "fmt ", 4) && off + 8 + 16 <= len) {
            *ch   = c[10] | c[11] << 8;
            *rate = c[12] | c[13] << 8 | c[14] << 16 | (uint32_t)c[15] << 24;
        } else if (!memcmp(c, "data", 4)) {
            return off + 8;
        }
        if (sz > (uint32_t)len) return -1;
        off += 8 + (int)sz + (sz & 1);
    }
    return -1;
}

static void start_playing(Stream *st) {
    i2s_open(st->rate);
    amp_enabled(true);
    st->t_first_sound = esp_timer_get_time();
    st->playing = true;
    feed_i2s(s_buf, st->staged, st->ch);
}

static void on_pcm(const uint8_t *d, int len) {
    Stream *st = &s_st;
    if (st->has_carry && len > 0) {
        uint8_t pair[2] = {st->carry, d[0]};
        st->has_carry = false;
        d++; len--;
        st->total_pcm += 2;
        if (st->playing) feed_i2s(pair, 2, st->ch);
        else if (st->staged + 2 <= MAX_WAV) { memcpy(s_buf + st->staged, pair, 2); st->staged += 2; }
    }
    if (len & 1) { st->carry = d[len - 1]; st->has_carry = true; len--; }
    if (len <= 0) return;
    st->total_pcm += len;

    if (st->playing) { feed_i2s(d, len, st->ch); return; }
    if (st->staged + len <= MAX_WAV) { memcpy(s_buf + st->staged, d, len); st->staged += len; }
    if (st->staged >= PREBUFFER_BYTES) start_playing(st);
}

static esp_err_t on_http(esp_http_client_event_t *e) {
    if (e->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    if (esp_http_client_get_status_code(e->client) != 200) {
        int n = e->data_len < 200 ? e->data_len : 200;
        ESP_LOGE(TAG, "HTTP %d: %.*s", esp_http_client_get_status_code(e->client),
                 n, (char *)e->data);
        return ESP_OK;
    }
    Stream *st = &s_st;
    const uint8_t *d = e->data;
    int len = e->data_len;

    if (!st->parsed) {
        int take = sizeof st->hdr - st->hdr_len;
        if (take > len) take = len;
        memcpy(st->hdr + st->hdr_len, d, take);
        st->hdr_len += take;
        int off = parse_riff(st->hdr, st->hdr_len, &st->rate, &st->ch);
        if (off < 0) return ESP_OK;                  // cabecera incompleta
        st->parsed = true;
        int extra = st->hdr_len - off;
        if (extra > 0) on_pcm(st->hdr + off, extra); // lo que ya era PCM
        d += take; len -= take;
        if (len <= 0) return ESP_OK;
    }
    on_pcm(d, len);
    return ESP_OK;
}

// Devuelve bytes de PCM reproducidos, o 0. Nunca registra la clave.
static int speak_streaming(const char *text) {
    if (!s_cli) {
        esp_http_client_config_t http = {
            .url = API,
            .method = HTTP_METHOD_POST,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = 20000,
            .buffer_size = 4096,
            .buffer_size_tx = 1024,
            .keep_alive_enable = true,     // el handshake cuesta ~1,8 s: no repetirlo
            .event_handler = on_http,
        };
        s_cli = esp_http_client_init(&http);
    }
    if (!s_cli) return 0;

    char body[TEXT_MAX * 2 + 256];
    char esc[TEXT_MAX * 2];
    size_t e = 0;
    for (const char *p = text; *p && e < sizeof esc - 2; p++) {
        if (*p == '"' || *p == '\\') esc[e++] = '\\';
        if ((unsigned char)*p < 0x20) continue;
        esc[e++] = *p;
    }
    esc[e] = 0;
    int n = snprintf(body, sizeof body,
                     "{\"model\":\"%s\",\"voice\":\"%s\",\"input\":\"%s\","
                     "\"response_format\":\"wav\"}",
                     CONFIG_BUDDY_TTS_MODEL, CONFIG_BUDDY_TTS_VOICE, esc);

    char auth[sizeof("Bearer ") + sizeof(CONFIG_BUDDY_OPENAI_API_KEY)];
    snprintf(auth, sizeof auth, "Bearer %s", CONFIG_BUDDY_OPENAI_API_KEY);
    esp_http_client_set_header(s_cli, "Authorization", auth);
    esp_http_client_set_header(s_cli, "Content-Type", "application/json");
    esp_http_client_set_post_field(s_cli, body, n);

    memset(&s_st, 0, sizeof s_st);
    s_st.rate = 24000; s_st.ch = 1;

    // perform() mantiene viva la conexión entre llamadas. La versión anterior
    // usaba open()/write()/read(), que abre una nueva cada vez.
    esp_err_t err = esp_http_client_perform(s_cli);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "perform: %s", esp_err_to_name(err));
        esp_http_client_close(s_cli);
    }
    if (!s_st.playing && s_st.staged > 0) start_playing(&s_st);   // frase muy corta
    amp_enabled(false);
    return s_st.total_pcm;
}

static void voice_task(void *arg) {
    char text[TEXT_MAX];
    for (;;) {
        if (xQueueReceive(s_q, text, portMAX_DELAY) != pdTRUE) continue;
        esp_netif_t *nif = esp_netif_get_default_netif();
        esp_netif_ip_info_t ip = {0};
        if (!nif || esp_netif_get_ip_info(nif, &ip) != ESP_OK || ip.ip.addr == 0) {
            ESP_LOGW(TAG, "sin red todavía; no se dice: %.40s", text);
            continue;
        }
        const int64_t t0 = esp_timer_get_time();
        int pcm = speak_streaming(text);
        const int64_t t_end = esp_timer_get_time();
        if (pcm <= 0) { ESP_LOGW(TAG, "sin audio para: %.40s", text); continue; }
        const float secs = (float)pcm / (s_st.rate * s_st.ch * 2);
        // El número que importa es el primero: cuánto tarda en oírse ALGO.
        ESP_LOGI(TAG, "primer sonido %lld ms · total %lld ms · %d B · %.1f s de audio",
                 (s_st.t_first_sound - t0) / 1000, (t_end - t0) / 1000, pcm, secs);
    }
}

int voice_start(void) {
    if (CONFIG_BUDDY_OPENAI_API_KEY[0] == '\0') {
        ESP_LOGW(TAG, "sin clave de OpenAI: el buddy no hablará "
                      "(menuconfig -> Buddy Zero -> OpenAI API key)");
        return 0;
    }
    s_buf = heap_caps_malloc(MAX_WAV, MALLOC_CAP_SPIRAM);
    if (!s_buf) { ESP_LOGE(TAG, "sin PSRAM para el audio"); return 0; }

    gpio_config_t io = {.pin_bit_mask = 1ULL << CONFIG_BUDDY_TTS_PIN_MUTE,
                        .mode = GPIO_MODE_OUTPUT};
    gpio_config(&io);
    amp_enabled(false);                  // callado hasta que haya algo que decir

    s_q = xQueueCreate(2, TEXT_MAX);
    if (!s_q) return 0;
    // Core 0: la cara vive en el 1 y no queremos que la red le robe ciclos.
    xTaskCreatePinnedToCore(voice_task, "voice", 8192, NULL, 4, NULL, 0);
    ESP_LOGI(TAG, "voz lista (modelo %s, voz %s)", CONFIG_BUDDY_TTS_MODEL,
             CONFIG_BUDDY_TTS_VOICE);
    return 1;
}

void voice_say(const char *text) {
    if (!s_q || !text || !*text) return;
    char item[TEXT_MAX];
    strncpy(item, text, TEXT_MAX - 1);
    item[TEXT_MAX - 1] = 0;
    // Sin esperar: si ya hay dos frases en cola, esta se descarta. Preferimos
    // perder una frase a acumular retardo entre lo que se ve y lo que se oye.
    if (xQueueSend(s_q, item, 0) != pdTRUE) ESP_LOGW(TAG, "cola llena, frase descartada");
}
