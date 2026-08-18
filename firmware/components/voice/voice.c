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

// Devuelve bytes recibidos, o 0. Nunca registra la clave.
static int fetch_tts(const char *text, int *out_rate, int *out_channels, int *out_pcm_off) {
    esp_http_client_config_t http = {
        .url = API,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 20000,
        .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&http);
    if (!cli) return 0;

    // El texto va tal cual dentro de JSON; hay que escapar comillas y barras o
    // una frase con comillas rompe la petición.
    char body[TEXT_MAX * 2 + 256];
    char esc[TEXT_MAX * 2];
    size_t e = 0;
    for (const char *p = text; *p && e < sizeof esc - 2; p++) {
        if (*p == '"' || *p == '\\') esc[e++] = '\\';
        if ((unsigned char)*p < 0x20) continue;      // control: fuera
        esc[e++] = *p;
    }
    esc[e] = 0;
    int n = snprintf(body, sizeof body,
                     "{\"model\":\"%s\",\"voice\":\"%s\",\"input\":\"%s\","
                     "\"response_format\":\"wav\"}",
                     CONFIG_BUDDY_TTS_MODEL, CONFIG_BUDDY_TTS_VOICE, esc);

    char auth[128];
    snprintf(auth, sizeof auth, "Bearer %s", CONFIG_BUDDY_OPENAI_API_KEY);
    esp_http_client_set_header(cli, "Authorization", auth);
    esp_http_client_set_header(cli, "Content-Type", "application/json");

    int got = 0;
    if (esp_http_client_open(cli, n) != ESP_OK) { esp_http_client_cleanup(cli); return 0; }
    if (esp_http_client_write(cli, body, n) != n) goto done;
    if (esp_http_client_fetch_headers(cli) < 0) goto done;

    int status = esp_http_client_get_status_code(cli);
    if (status != 200) {
        // El cuerpo del error trae el motivo (clave mala, cuota, modelo…) y no
        // contiene la clave, así que se puede registrar entero.
        int r = esp_http_client_read(cli, (char *)s_buf, 255);
        if (r > 0) { s_buf[r] = 0; ESP_LOGE(TAG, "HTTP %d: %s", status, (char *)s_buf); }
        else ESP_LOGE(TAG, "HTTP %d, sin cuerpo", status);
        goto done;
    }
    while (got < MAX_WAV) {
        int r = esp_http_client_read(cli, (char *)s_buf + got, MAX_WAV - got);
        if (r <= 0) break;
        got += r;
    }
done:
    esp_http_client_close(cli);
    esp_http_client_cleanup(cli);
    if (got < 44) return 0;

    // RIFF: buscar los trozos `fmt ` y `data` en vez de dar por hecho el
    // desplazamiento 44 — algunos servidores meten un `LIST` por medio.
    *out_rate = 24000; *out_channels = 1; *out_pcm_off = 44;
    for (int off = 12; off + 8 <= got;) {
        const uint8_t *c = s_buf + off;
        uint32_t sz = c[4] | c[5] << 8 | c[6] << 16 | (uint32_t)c[7] << 24;
        if (memcmp(c, "fmt ", 4) == 0 && off + 8 + 16 <= got) {
            *out_channels = c[10] | c[11] << 8;
            *out_rate = c[12] | c[13] << 8 | c[14] << 16 | (uint32_t)c[15] << 24;
        } else if (memcmp(c, "data", 4) == 0) {
            *out_pcm_off = off + 8;
            if (sz && off + 8 + (int)sz <= got) got = off + 8 + sz;
            break;
        }
        off += 8 + sz + (sz & 1);
    }
    return got;
}

static void play(const uint8_t *pcm, int bytes, int channels) {
    // El MAX98357A está cableado en estéreo; un WAV mono hay que duplicarlo a
    // los dos canales o suena a mitad de velocidad.
    enum { FRAMES = 512 };
    static int16_t out[FRAMES * 2];
    const int16_t *in = (const int16_t *)pcm;
    int samples = bytes / 2;
    amp_enabled(true);
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
    amp_enabled(false);
}

static void voice_task(void *arg) {
    char text[TEXT_MAX];
    for (;;) {
        if (xQueueReceive(s_q, text, portMAX_DELAY) != pdTRUE) continue;
        int rate, ch, off;
        const int64_t t0 = esp_timer_get_time();
        int bytes = fetch_tts(text, &rate, &ch, &off);
        const int64_t t_api = esp_timer_get_time() - t0;
        if (bytes <= off) { ESP_LOGW(TAG, "sin audio para: %.40s", text); continue; }
        const int pcm = bytes - off;
        const float secs = (float)pcm / (rate * ch * 2);
        i2s_open(rate);
        const int64_t t1 = esp_timer_get_time();
        play(s_buf + off, pcm, ch);
        ESP_LOGI(TAG, "API %.0f ms · %d B · %d Hz %s · %.1f s de audio · reproducir %.0f ms",
                 t_api / 1000.0, pcm, rate, ch == 2 ? "estéreo" : "mono", secs,
                 (esp_timer_get_time() - t1) / 1000.0);
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
