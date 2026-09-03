// Layout: vocabulary in an arena plus an open hash, transitions in a SORTED
// array of (key words..., next). Finding a state is a binary search; the run
// of equal rows is contiguous, so picking a continuation is a random index
// inside it and repeated rows ARE their own weight. No pointers: they would
// triple the size and scatter the PSRAM accesses, which is what dominates the
// cost on this chip.
//
// Everything is sized from the input in a counting pass, so there is no
// realloc path and no fixed ceiling to overrun. That is not tidiness: the
// corpus comes from a pack, and a pack is untrusted.
#include "markov.h"

#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
static void* mv_alloc(size_t n) {
  void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
  return p ? p : heap_caps_malloc(n, MALLOC_CAP_DEFAULT);
}
#else
static void* mv_alloc(size_t n) { return malloc(n); }
#endif

#define ID_END   0
#define ID_BEGIN 1
#define N_RESERVED 2

// Fixed-width key so `order` can vary at runtime. Unused slots stay ID_BEGIN
// and never take part in a comparison -- see key_cmp.
typedef struct {
  uint16_t k[MARKOV_MAX_ORDER];
  uint16_t next;
} Row;

struct markov_chain {
  char*     arena;
  uint32_t  arena_len;
  uint32_t* off;            // id -> offset into the arena
  uint32_t  words;          // distinct words, including the two reserved ids
  int32_t*  hash;
  uint32_t  hmask;
  Row*      rows;
  uint32_t  n_rows;
  uint32_t  lines;
  uint8_t   order;
  uint16_t  max_words;
  uint32_t  rng;
  size_t    bytes;
};

static uint32_t fnv(const char* s, uint32_t len) {
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < len; i++) { h ^= (uint8_t)s[i]; h *= 16777619u; }
  return h;
}

// The blob may carry a trailing NUL: EMBED_TXTFILES appends one, and a file
// read off LittleFS can too. Without '\0' as a separator it interns as a word
// and any sentence that picks it comes out empty -- it showed up in 2% of
// generations and took a while to find.
static int is_sep(char c) { return c == ' ' || c == '\r' || c == '\t' || c == '\0'; }

// ---------------------------------------------------------------- counting

typedef struct { uint32_t tokens, lines, word_bytes; } Counts;

static void count_part(const char* p, const char* end, Counts* c) {
  while (p < end) {
    const char* nl = memchr(p, '\n', (size_t)(end - p));
    const char* stop = nl ? nl : end;
    const char* w = p;
    int any = 0;
    while (w < stop) {
      while (w < stop && is_sep(*w)) w++;
      const char* ws = w;
      while (w < stop && !is_sep(*w)) w++;
      if (w == ws) break;
      c->tokens++;
      c->word_bytes += (uint32_t)(w - ws) + 1;   // + NUL
      any = 1;
    }
    if (any) c->lines++;
    if (!nl) break;                              // never form end + 1
    p = nl + 1;
  }
}

static uint32_t next_pow2(uint32_t v) {
  uint32_t p = 8;
  while (p < v) p <<= 1;
  return p;
}

// ---------------------------------------------------------------- vocabulary

// Returns the id, or 0xFFFF if the vocabulary is full. The caller must check:
// silently truncating a 16-bit id corrupts the chain instead of failing it.
static uint16_t intern(markov_chain_t* c, const char* s, uint32_t len, uint32_t word_cap) {
  uint32_t i = fnv(s, len) & c->hmask;
  uint32_t probes = 0;
  while (c->hash[i] >= 0) {
    const char* w = c->arena + c->off[c->hash[i]];
    if (strlen(w) == len && memcmp(w, s, len) == 0) return (uint16_t)c->hash[i];
    i = (i + 1) & c->hmask;
    if (++probes > c->hmask) return 0xFFFF;      // table full: never spin
  }
  if (c->words >= word_cap || c->words >= 0xFFFF) return 0xFFFF;
  uint32_t off = c->arena_len;
  memcpy(c->arena + off, s, len);
  c->arena[off + len] = 0;
  c->arena_len += len + 1;
  c->off[c->words] = off;
  c->hash[i] = (int32_t)c->words;
  return (uint16_t)c->words++;
}

// ---------------------------------------------------------------- filling

static int fill_part(markov_chain_t* c, const char* p, const char* end,
                     uint32_t row_cap, uint32_t word_cap) {
  const uint8_t order = c->order;
  while (p < end) {
    const char* nl = memchr(p, '\n', (size_t)(end - p));
    const char* stop = nl ? nl : end;
    uint16_t st[MARKOV_MAX_ORDER];
    for (uint8_t i = 0; i < MARKOV_MAX_ORDER; i++) st[i] = ID_BEGIN;
    const char* w = p;
    int any = 0;
    while (w < stop) {
      while (w < stop && is_sep(*w)) w++;
      const char* ws = w;
      while (w < stop && !is_sep(*w)) w++;
      if (w == ws) break;
      uint16_t id = intern(c, ws, (uint32_t)(w - ws), word_cap);
      if (id == 0xFFFF) return 0;
      if (c->n_rows >= row_cap) return 0;        // counting pass was wrong: refuse
      Row* r = &c->rows[c->n_rows++];
      for (uint8_t i = 0; i < order; i++) r->k[i] = st[i];
      for (uint8_t i = order; i < MARKOV_MAX_ORDER; i++) r->k[i] = ID_BEGIN;
      r->next = id;
      for (uint8_t i = 0; i + 1 < order; i++) st[i] = st[i + 1];
      st[order - 1] = id;
      any = 1;
    }
    if (any) {
      if (c->n_rows >= row_cap) return 0;
      Row* r = &c->rows[c->n_rows++];
      for (uint8_t i = 0; i < order; i++) r->k[i] = st[i];
      for (uint8_t i = order; i < MARKOV_MAX_ORDER; i++) r->k[i] = ID_BEGIN;
      r->next = ID_END;
      c->lines++;
    }
    if (!nl) break;
    p = nl + 1;
  }
  return 1;
}

// ---------------------------------------------------------------- ordering

// Always compares the full width, never just `order`. Slots beyond the order
// hold ID_BEGIN in every row and in every lookup key, so they can never change
// the outcome -- and comparing them keeps the comparator free of the chain,
// which is what lets qsort stay reentrant without qsort_r.
static int key_cmp_n(const uint16_t* x, const uint16_t* y) {
  for (uint8_t i = 0; i < MARKOV_MAX_ORDER; i++)
    if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
  return 0;
}

static int row_cmp(const void* a, const void* b) {
  const Row* p = a;
  const Row* q = b;
  int r = key_cmp_n(p->k, q->k);
  if (r) return r;
  return p->next < q->next ? -1 : (p->next > q->next);
}

// First row whose key is >= `key`.
static uint32_t lower_bound(const markov_chain_t* c, const uint16_t* key) {
  uint32_t lo = 0, hi = c->n_rows;
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    if (key_cmp_n(c->rows[mid].k, key) < 0) lo = mid + 1; else hi = mid;
  }
  return lo;
}

// First row whose key is > `key`. A binary search rather than a scan: the run
// for (BEGIN, BEGIN...) is exactly one row per line, so scanning it would make
// every sentence cost O(lines) -- 5 us at 371 lines, ~750 us at 5000.
static uint32_t upper_bound(const markov_chain_t* c, const uint16_t* key) {
  uint32_t lo = 0, hi = c->n_rows;
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    if (key_cmp_n(c->rows[mid].k, key) <= 0) lo = mid + 1; else hi = mid;
  }
  return lo;
}

// ---------------------------------------------------------------- build

markov_chain_t* markov_build(const markov_corpus_t* parts, size_t n,
                             const markov_cfg_t* cfg) {
  if (!parts || n == 0 || !cfg) return NULL;
  if (cfg->order < 1 || cfg->order > MARKOV_MAX_ORDER) return NULL;
  if (cfg->max_words == 0) return NULL;

  Counts ct = {0, 0, 0};
  for (size_t i = 0; i < n; i++) {
    if (!parts[i].text || parts[i].len == 0) continue;
    count_part(parts[i].text, parts[i].text + parts[i].len, &ct);
  }
  if (ct.tokens == 0 || ct.lines == 0) return NULL;

  // Exact upper bounds. Every token contributes one row, every line one more
  // for the END transition; every token is at most one new word.
  const uint32_t row_cap  = ct.tokens + ct.lines;
  const uint32_t word_cap = ct.tokens + N_RESERVED;
  if (word_cap >= 0xFFFF) return NULL;           // ids are 16 bit, by design
  const uint32_t hash_n = next_pow2(word_cap * 2);

  const size_t bytes = (size_t)ct.word_bytes
                     + (size_t)word_cap * sizeof(uint32_t)
                     + (size_t)hash_n   * sizeof(int32_t)
                     + (size_t)row_cap  * sizeof(Row);
  if (cfg->max_bytes && bytes > cfg->max_bytes) return NULL;

  markov_chain_t* c = calloc(1, sizeof *c);
  if (!c) return NULL;
  c->order     = cfg->order;
  c->max_words = cfg->max_words;
  c->rng       = cfg->seed ? cfg->seed : 0x9E3779B9u;
  c->bytes     = bytes;
  c->hmask     = hash_n - 1;

  c->arena = mv_alloc(ct.word_bytes ? ct.word_bytes : 1);
  c->off   = mv_alloc((size_t)word_cap * sizeof *c->off);
  c->hash  = mv_alloc((size_t)hash_n * sizeof *c->hash);
  c->rows  = mv_alloc((size_t)row_cap * sizeof *c->rows);
  if (!c->arena || !c->off || !c->hash || !c->rows) { markov_free(c); return NULL; }

  for (uint32_t i = 0; i < hash_n; i++) c->hash[i] = -1;
  // ids 0 and 1 are reserved (END, BEGIN) and are never looked up
  c->off[0] = 0;
  c->off[1] = 0;
  c->words  = N_RESERVED;

  for (size_t i = 0; i < n; i++) {
    if (!parts[i].text || parts[i].len == 0) continue;
    if (!fill_part(c, parts[i].text, parts[i].text + parts[i].len, row_cap, word_cap)) {
      markov_free(c);
      return NULL;
    }
  }
  if (c->n_rows == 0) { markov_free(c); return NULL; }

  qsort(c->rows, c->n_rows, sizeof(Row), row_cmp);
  return c;
}

void markov_free(markov_chain_t* c) {
  if (!c) return;
  free(c->arena);
  free(c->off);
  free(c->hash);
  free(c->rows);
  free(c);
}

// ---------------------------------------------------------------- generate

static uint32_t xrand(markov_chain_t* c) {
  c->rng ^= c->rng << 13;
  c->rng ^= c->rng >> 17;
  c->rng ^= c->rng << 5;
  return c->rng;
}

size_t markov_generate(markov_chain_t* c, char* out, size_t cap) {
  if (!out || cap == 0) return 0;
  out[0] = 0;
  if (!c || cap < 2) return 0;

  uint16_t st[MARKOV_MAX_ORDER];
  for (uint8_t i = 0; i < MARKOV_MAX_ORDER; i++) st[i] = ID_BEGIN;
  size_t len = 0;

  for (uint16_t i = 0; i < c->max_words; i++) {
    const uint32_t lo = lower_bound(c, st);
    const uint32_t hi = upper_bound(c, st);
    if (hi == lo) break;
    const uint16_t nx = c->rows[lo + xrand(c) % (hi - lo)].next;
    if (nx == ID_END) break;
    const char* w = c->arena + c->off[nx];
    const size_t wl = strlen(w);
    if (len + wl + 2 >= cap) break;
    if (len) out[len++] = ' ';
    memcpy(out + len, w, wl);
    len += wl;
    for (uint8_t k = 0; k + 1 < c->order; k++) st[k] = st[k + 1];
    st[c->order - 1] = nx;
  }
  out[len] = 0;
  return len;
}

size_t markov_bytes(const markov_chain_t* c)  { return c ? c->bytes : 0; }
size_t markov_lines(const markov_chain_t* c)  { return c ? c->lines : 0; }
size_t markov_states(const markov_chain_t* c) { return c ? c->n_rows : 0; }
