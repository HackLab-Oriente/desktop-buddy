// Markov-chain sentence generator.
//
// This is a LIBRARY, not a subsystem: it knows nothing about packs,
// expressions, pools or the event bus, it owns no corpus, and it never
// touches the bus. The layer above it owns the written banks, rolls against
// `mix`, suppresses repeats and falls back to a written line -- all of which
// need to see the bank, which this file deliberately never does.
//
// Measured on device (spikes/markov-s3, order 2, 9.4 KB of corpus): ~19 KB of
// rows and vocabulary, a few ms to build, ~46 us per sentence. At that price
// nothing here needs caching.
//
// Order 2 is the working point: it keeps agreement almost always, and what
// breaks is the sense rather than the grammar (spikes/markov-frases).
//
// THREADING: a chain is immutable after markov_build() EXCEPT for its random
// state, which markov_generate() advances. Two threads generating from the
// same chain is a data race. Serialise per chain -- in this firmware that
// means calling from the bus dispatch task, which the event registry already
// guarantees never runs handlers in parallel.
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The longest key the row layout can hold. Rows are fixed width at any order,
// so order 1 and 3 cost the same as order 2: 8 bytes per row against the 6 a
// hardcoded order 2 would need. That 33% buys per-expression `order`, which
// pack-format promises (1..3) and which a fixed layout cannot deliver.
#define MARKOV_MAX_ORDER 3

typedef struct markov_chain markov_chain_t;

// One piece of corpus. Borrowed for the duration of markov_build() only --
// the chain copies what it needs and never refers to `text` again.
typedef struct {
  const char* text;
  size_t len;
} markov_corpus_t;

typedef struct {
  uint8_t  order;       // 1..MARKOV_MAX_ORDER; anything else fails the build
  uint16_t max_words;   // cuts the classic failure: the sentence that never ends
  uint32_t seed;        // 0 is replaced by a fixed non-zero value
  uint32_t max_bytes;   // hard budget; the build fails rather than overrunning it
} markov_cfg_t;

// Builds a chain from `n` corpus pieces. Returns NULL on any failure -- bad
// config, empty corpus, allocation failure, budget exceeded, or a vocabulary
// larger than the id space. Never aborts, never leaves partial state: corpus
// text is pack data, and a hostile or merely sloppy pack must produce a quiet
// buddy, never a crash.
markov_chain_t* markov_build(const markov_corpus_t* parts, size_t n,
                             const markov_cfg_t* cfg);

void markov_free(markov_chain_t* c);

// Writes one sentence into `out` and returns its length. Returns 0 if nothing
// could be generated. `out` is always NUL-terminated when cap >= 1, including
// on failure, so a caller that ignores the return value cannot print
// uninitialised memory.
size_t markov_generate(markov_chain_t* c, char* out, size_t cap);

// Bytes actually held by the chain. For the boot log and for budgeting.
size_t markov_bytes(const markov_chain_t* c);

// How many distinct sentences the chain can reach is not knowable cheaply,
// but these two say how much material it has, which is what predicts variety:
// measured novelty was 12.8% at 60 lines of corpus and 44.7% at 920.
size_t markov_lines(const markov_chain_t* c);
size_t markov_states(const markov_chain_t* c);

#ifdef __cplusplus
}
#endif
