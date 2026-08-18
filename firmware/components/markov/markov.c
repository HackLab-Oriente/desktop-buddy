// Cadena de Markov para las frases del buddy — el motor medido en
// spikes/markov-s3, empaquetado como componente.
//
// Disposición: vocabulario en arena + hash abierto, transiciones en un array
// ORDENADO de (w1, w2, siguiente) de 3 x uint16. Buscar el estado es una
// búsqueda binaria; la corrida de filas iguales es contigua, así que elegir
// una continuación es un índice al azar dentro de ella y las repeticiones de
// una fila SON su peso. Sin punteros: triplicarían el tamaño y dispersarían
// los accesos a PSRAM, que es lo que domina el coste en este chip.
#include "markov.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

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


// --- estado del componente -------------------------------------------------
// Una cadena por registro, más una con todo combinado. Construir las ocho
// cuesta ~25 ms al arrancar y permite que un pack pida `pool: registro` o el
// banco suelto sin recalcular nada.
static Chain s_chain[NBANKS + 1];
static Vocab s_vocab[NBANKS + 1];
static Recent s_recent[NBANKS + 1];
static int s_ready = 0;

const char* markov_register(int i) {
    return (i >= 0 && i < (int)NBANKS) ? BANKS[i].name : NULL;
}

static int build_one(int slot, const Bank* banks, int nbanks) {
    Vocab* v = &s_vocab[slot];
    vocab_init(v, 13);
    if (!v->arena || !v->off || !v->hash) return 0;
    Chain* c = &s_chain[slot];
    memset(c, 0, sizeof *c);
    c->cap = 1024;
    c->rows = malloc(c->cap * sizeof(Row));
    c->v = v;
    if (!c->rows) return 0;
    for (int i = 0; i < nbanks; i++) chain_add_bank(c, banks[i].s, banks[i].e);
    chain_finish(c);
    // compactar a PSRAM: la tabla se construye creciendo en RAM interna
    Row* packed = psram(c->n * sizeof(Row));
    if (packed) { memcpy(packed, c->rows, c->n * sizeof(Row)); free(c->rows); c->rows = packed; }
    return 1;
}

int markov_start(void) {
    int64_t t0 = esp_timer_get_time();
    for (unsigned i = 0; i < NBANKS; i++)
        if (!build_one(i, &BANKS[i], 1)) { ESP_LOGE(TAG, "sin memoria para %s", BANKS[i].name); return 0; }
    if (!build_one(NBANKS, BANKS, NBANKS)) { ESP_LOGE(TAG, "sin memoria para el combinado"); return 0; }
    s_ready = 1;
    ESP_LOGI(TAG, "%u registros + combinado (%"PRIu32" frases, %"PRIu32" transiciones) en %.0f ms",
             (unsigned)NBANKS, s_chain[NBANKS].lines, s_chain[NBANKS].n,
             (esp_timer_get_time() - t0) / 1000.0);
    return 1;
}

int markov_say(const char* reg, char* out, size_t cap) {
    if (!s_ready || cap < 2) return 0;
    int slot = (int)NBANKS;                     // por defecto, todos combinados
    if (reg) {
        for (unsigned i = 0; i < NBANKS; i++)
            if (strcmp(reg, BANKS[i].name) == 0) { slot = (int)i; break; }
    }
    Chain* c = &s_chain[slot];
    Recent* r = &s_recent[slot];
    // Hasta 8 intentos de no repetir; si el banco es pequeño puede no haber
    // alternativa, y entonces preferimos repetir a quedarnos mudos.
    for (int t = 0; t < 8; t++) {
        int len = generate(c, out, cap);
        if (len == 0) continue;
        uint32_t h = fnv(out, (uint32_t)len);
        if (!recent_hit(r, h) || t == 7) { recent_push(r, h); return len; }
    }
    return generate(c, out, cap);               // nunca mudo
}
