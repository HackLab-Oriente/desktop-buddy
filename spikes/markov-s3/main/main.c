// Spike: la cadena de Markov corriendo de verdad en el ESP32-S3.
//
// Mide lo que hasta ahora era una estimación analítica: cuánto ocupa, cuánto
// tarda en construirse, y cuánto cuesta devolver una frase — combinando los
// bancos de varios registros, con supresión de frases repetidas.
//
// Disposición en memoria (la que propone spikes/markov-frases/README.md):
//   vocabulario : arena de strings + tabla hash abierta -> id uint16
//   transiciones: array ORDENADO de (w1, w2, siguiente), 3 x uint16 = 6 B
//   generar     : búsqueda binaria del estado, la corrida de filas iguales es
//                 contigua, y se elige una al azar dentro de ella. Las
//                 repeticiones de una fila SON su peso; no hay contadores.
//
// Sin punteros en la tabla a propósito: triplicarían el tamaño y dispersarían
// los accesos a PSRAM, que es justo lo que domina el coste en este chip.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "markov"
#define ID_END   0
#define ID_BEGIN 1
#define ORDER    2
#define MAX_WORDS 20          // corta el fallo típico: la frase que no termina
#define NO_REPEAT 16          // cuántas frases recientes no se repiten

// --- ficheros embebidos: un banco por registro -----------------------------
#define REG(n) extern const uint8_t n##_start[] asm("_binary_" #n "_txt_start"); \
               extern const uint8_t n##_end[]   asm("_binary_" #n "_txt_end");
REG(calido) REG(jugueton) REG(curioso) REG(urgente) REG(seco) REG(sonoliento) REG(llano)
#undef REG

typedef struct { const char *name; const uint8_t *s, *e; } Bank;
static const Bank BANKS[] = {
    {"cálido",     calido_start,     calido_end},
    {"juguetón",   jugueton_start,   jugueton_end},
    {"curioso",    curioso_start,    curioso_end},
    {"urgente",    urgente_start,    urgente_end},
    {"seco",       seco_start,       seco_end},
    {"soñoliento", sonoliento_start, sonoliento_end},
    {"llano",      llano_start,      llano_end},
};
#define NBANKS (sizeof BANKS / sizeof BANKS[0])

// --- vocabulario -----------------------------------------------------------
typedef struct {
    char    *arena;  uint32_t arena_len, arena_cap;
    uint32_t *off;   uint32_t n, cap;      // id -> offset en la arena
    int32_t  *hash;  uint32_t hmask;       // hash abierto -> id
} Vocab;

static uint32_t fnv(const char *s, uint32_t len) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < len; i++) { h ^= (uint8_t)s[i]; h *= 16777619u; }
    return h;
}

static void *psram(size_t n) {
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    return p ? p : heap_caps_malloc(n, MALLOC_CAP_DEFAULT);
}

static void vocab_init(Vocab *v, uint32_t hbits) {
    v->arena_cap = 64 * 1024; v->arena = psram(v->arena_cap); v->arena_len = 0;
    v->cap = 4096; v->off = psram(v->cap * sizeof *v->off); v->n = 0;
    v->hmask = (1u << hbits) - 1;
    v->hash = psram((v->hmask + 1) * sizeof *v->hash);
    for (uint32_t i = 0; i <= v->hmask; i++) v->hash[i] = -1;
    // los ids 0 y 1 están reservados (END, BEGIN) y nunca se buscan
    v->off[v->n++] = 0; v->off[v->n++] = 0;
}

static uint16_t vocab_intern(Vocab *v, const char *s, uint32_t len) {
    uint32_t i = fnv(s, len) & v->hmask;
    while (v->hash[i] >= 0) {
        const char *c = v->arena + v->off[v->hash[i]];
        if (strlen(c) == len && memcmp(c, s, len) == 0) return (uint16_t)v->hash[i];
        i = (i + 1) & v->hmask;
    }
    if (v->arena_len + len + 1 > v->arena_cap) { ESP_LOGE(TAG, "arena llena"); abort(); }
    uint32_t off = v->arena_len;
    memcpy(v->arena + off, s, len); v->arena[off + len] = 0;
    v->arena_len += len + 1;
    v->off[v->n] = off;
    v->hash[i] = (int32_t)v->n;
    return (uint16_t)v->n++;
}

// --- tabla de transiciones -------------------------------------------------
typedef struct { uint16_t a, b, next; } Row;
typedef struct {
    Row *rows; uint32_t n, cap;
    Vocab *v;
    uint32_t lines, words;
} Chain;

static int row_cmp(const void *x, const void *y) {
    const Row *p = x, *q = y;
    if (p->a != q->a) return p->a < q->a ? -1 : 1;
    if (p->b != q->b) return p->b < q->b ? -1 : 1;
    return p->next < q->next ? -1 : (p->next > q->next);
}

static void chain_add_bank(Chain *c, const uint8_t *s, const uint8_t *e) {
    const char *p = (const char *)s, *end = (const char *)e;
    while (p < end) {
        const char *nl = memchr(p, '\n', end - p);
        if (!nl) nl = end;
        uint16_t st[ORDER] = {ID_BEGIN, ID_BEGIN};
        const char *w = p;
        int any = 0;
        // EMBED_TXTFILES añade un '\0' al final del blob: sin este filtro se
        // interna como palabra, y una frase que lo elija sale vacía. Salía en
        // el 2 %% de las generaciones y costó encontrarlo.
        #define SEP(ch) ((ch) == ' ' || (ch) == '\r' || (ch) == '\0')
        while (w < nl) {
            while (w < nl && SEP(*w)) w++;
            const char *ws = w;
            while (w < nl && !SEP(*w)) w++;
            if (w == ws) break;
            uint16_t id = vocab_intern(c->v, ws, (uint32_t)(w - ws));
            if (c->n == c->cap) { c->cap *= 2; c->rows = realloc(c->rows, c->cap * sizeof(Row)); }
            c->rows[c->n++] = (Row){st[0], st[1], id};
            st[0] = st[1]; st[1] = id;
            c->words++; any = 1;
        }
        if (any) {
            if (c->n == c->cap) { c->cap *= 2; c->rows = realloc(c->rows, c->cap * sizeof(Row)); }
            c->rows[c->n++] = (Row){st[0], st[1], ID_END};
            c->lines++;
        }
        p = nl + 1;
    }
}

static void chain_finish(Chain *c) { qsort(c->rows, c->n, sizeof(Row), row_cmp); }

// primera fila con (a,b) >= buscado
static uint32_t lower_bound(const Chain *c, uint16_t a, uint16_t b) {
    uint32_t lo = 0, hi = c->n;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        const Row *r = &c->rows[mid];
        if (r->a < a || (r->a == a && r->b < b)) lo = mid + 1; else hi = mid;
    }
    return lo;
}

static uint32_t rng_state = 0x12345678;
static uint32_t xrand(void) {
    rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5;
    return rng_state;
}

// Devuelve la longitud escrita en out. La corrida de filas con el mismo (a,b)
// es contigua, así que elegir es un índice al azar dentro de ella.
static int generate(const Chain *c, char *out, size_t cap) {
    uint16_t st[ORDER] = {ID_BEGIN, ID_BEGIN};
    size_t len = 0;
    for (int i = 0; i < MAX_WORDS; i++) {
        uint32_t lo = lower_bound(c, st[0], st[1]);
        uint32_t hi = lo;
        while (hi < c->n && c->rows[hi].a == st[0] && c->rows[hi].b == st[1]) hi++;
        if (hi == lo) break;
        uint16_t nx = c->rows[lo + xrand() % (hi - lo)].next;
        if (nx == ID_END) break;
        const char *w = c->v->arena + c->v->off[nx];
        size_t wl = strlen(w);
        if (len + wl + 2 >= cap) break;
        if (len) out[len++] = ' ';
        memcpy(out + len, w, wl); len += wl;
        st[0] = st[1]; st[1] = nx;
    }
    out[len] = 0;
    return (int)len;
}

// --- supresión de repetidas: anillo de hashes ------------------------------
typedef struct { uint32_t h[NO_REPEAT]; int i; } Recent;
static int recent_hit(Recent *r, uint32_t h) {
    for (int i = 0; i < NO_REPEAT; i++) if (r->h[i] == h) return 1;
    return 0;
}
static void recent_push(Recent *r, uint32_t h) { r->h[r->i++ % NO_REPEAT] = h; r->i %= NO_REPEAT; }

static int cmp_i64(const void *a, const void *b) {
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

static size_t free_psram(void) { return heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }

static void build_and_report(const char *label, const Bank *banks, int nbanks) {
    size_t before = free_psram();
    int64_t t0 = esp_timer_get_time();

    Vocab *v = psram(sizeof *v); vocab_init(v, 13);
    Chain c = {0};
    c.cap = 4096; c.rows = malloc(c.cap * sizeof(Row)); c.v = v;
    for (int i = 0; i < nbanks; i++) chain_add_bank(&c, banks[i].s, banks[i].e);
    int64_t t_parse = esp_timer_get_time() - t0;
    int64_t t1 = esp_timer_get_time();
    chain_finish(&c);
    int64_t t_sort = esp_timer_get_time() - t1;

    // la tabla se construye en RAM interna (realloc) y se compacta a PSRAM
    Row *packed = psram(c.n * sizeof(Row));
    memcpy(packed, c.rows, c.n * sizeof(Row));
    free(c.rows); c.rows = packed;
    size_t reserved = before - free_psram();
    // Lo que de verdad ocuparía un puerto ajustado: filas + arena + offsets.
    // `reserved` incluye la holgura que pido de más al arrancar (arena y tabla
    // hash con capacidad fija), y confundir las dos cifras sería engañarse.
    size_t footprint = c.n * sizeof(Row) + v->arena_len + v->n * sizeof(uint32_t);

    ESP_LOGI(TAG, "--- %s ---", label);
    ESP_LOGI(TAG, "  bancos %d · %"PRIu32" frases · %"PRIu32" palabras · %"PRIu32" únicas",
             nbanks, c.lines, c.words, v->n - 2);
    ESP_LOGI(TAG, "  transiciones %"PRIu32" x 6 B = %.1f KB   vocab %.1f KB",
             c.n, c.n * 6 / 1024.0, (v->arena_len + v->n * 4) / 1024.0);
    ESP_LOGI(TAG, "  HUELLA REAL %.1f KB   (reservado con holgura: %.1f KB)",
             footprint / 1024.0, reserved / 1024.0);
    ESP_LOGI(TAG, "  construir: parse %.1f ms + ordenar %.1f ms = %.1f ms",
             t_parse / 1000.0, t_sort / 1000.0, (t_parse + t_sort) / 1000.0);

    // --- generación ---------------------------------------------------------
    enum { N = 300 };
    static int64_t dt[N];
    static char buf[256];
    Recent recent = {0};
    int retries = 0, distinct_hits = 0, empties = 0;
    int64_t tg = esp_timer_get_time();
    for (int i = 0; i < N; i++) {
        int64_t a = esp_timer_get_time();
        int tries = 0;
        uint32_t h;
        do {
            if (generate(&c, buf, sizeof buf) == 0) empties++;
            h = fnv(buf, strlen(buf));
            tries++;
        } while (recent_hit(&recent, h) && tries < 8);
        recent_push(&recent, h);
        dt[i] = esp_timer_get_time() - a;
        retries += tries - 1;
        if (tries >= 8) distinct_hits++;
    }
    int64_t total = esp_timer_get_time() - tg;
    qsort(dt, N, sizeof dt[0], cmp_i64);
    ESP_LOGI(TAG, "  generar %d frases (con supresión de %d recientes): %.1f ms totales",
             N, NO_REPEAT, total / 1000.0);
    ESP_LOGI(TAG, "  por frase: p50 %"PRId64" us · p99 %"PRId64" us · max %"PRId64" us",
             dt[N/2], dt[(N*99)/100], dt[N-1]);
    ESP_LOGI(TAG, "  reintentos por repetida: %d · agotados: %d · vacías: %d",
             retries, distinct_hits, empties);
    // ¿existe la transición (INICIO,INICIO)->FIN? Sería una frase sin palabras.
    uint32_t lo0 = lower_bound(&c, ID_BEGIN, ID_BEGIN), bad = 0;
    for (uint32_t i = lo0; i < c.n && c.rows[i].a == ID_BEGIN && c.rows[i].b == ID_BEGIN; i++)
        if (c.rows[i].next == ID_END) bad++;
    ESP_LOGI(TAG, "  arranques que terminan sin decir nada: %"PRIu32, bad);

    for (int i = 0; i < 4; i++) { generate(&c, buf, sizeof buf); ESP_LOGI(TAG, "  > %s", buf); }

    free(v->arena); free(v->off); free(v->hash); free(v); free(c.rows);
}

void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(300));
    size_t corpus_bytes = 0;
    for (unsigned i = 0; i < NBANKS; i++) corpus_bytes += BANKS[i].e - BANKS[i].s;
    ESP_LOGI(TAG, "=== Markov en el S3 ===");
    ESP_LOGI(TAG, "corpus embebido: %u ficheros, %u B de flash",
             (unsigned)NBANKS, (unsigned)corpus_bytes);
    ESP_LOGI(TAG, "PSRAM libre al arrancar: %u B", (unsigned)free_psram());

    // 1) un banco solo — lo que hace un pack sin `pool`
    build_and_report("SOLO cálido (un fichero)", &BANKS[0], 1);
    // 2) los siete combinados — `pool: registro` llevado al extremo
    build_and_report("LOS 7 BANCOS COMBINADOS", BANKS, NBANKS);

    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
