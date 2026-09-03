// Host test for the Markov generator.
//
// This code walks bytes that come from a pack, and a pack is untrusted -- the
// same class as the NDEF decoder and the Latin-1 normaliser, so it runs under
// ASan and UBSan for the same reason. The cases below are not decoration:
// every one of them is a defect the reviewed spike version actually had.
//
//   - the offset table was written with no bounds check at all, so the 4095th
//     unique word walked off a fixed 16 KB block and corrupted the heap
//   - the open hash had no probe bound, so a full table span forever
//   - an over-long word hit abort(), turning a corpus edit into a boot loop
//   - realloc's return was never checked
//   - the RNG was a fixed constant, so every boot said the same thing
//
// Build: markov.c is C, so it gets its own compile step and then links.
// The exact commands live in .github/workflows/build.yml, "Markov generator".
#include "markov.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <string>

static int g_fail = 0;
static int g_checks = 0;

static void check(bool ok, const char* what) {
  ++g_checks;
  if (!ok) {
    std::printf("  FAIL: %s\n", what);
    ++g_fail;
  }
}

static markov_cfg_t cfg(uint8_t order = 2, uint32_t max_bytes = 0, uint32_t seed = 12345) {
  markov_cfg_t c{};
  c.order = order;
  c.max_words = 20;
  c.seed = seed;
  c.max_bytes = max_bytes;
  return c;
}

static markov_corpus_t part(const std::string& s) {
  return markov_corpus_t{s.data(), s.size()};
}

// A corpus with enough shared bigrams to actually recombine.
static const std::string kCorpus =
    "ahi estas te estaba esperando\n"
    "ahi estas otra vez\n"
    "te estaba buscando a ti\n"
    "otra vez por favor\n"
    "por favor no pares\n"
    "no pares ahi estas\n";

int main() {
  // ---- rejects bad configuration rather than trusting it -------------------
  {
    auto c = part(kCorpus);
    markov_cfg_t bad = cfg();
    bad.order = 0;
    check(markov_build(&c, 1, &bad) == nullptr, "order 0 is refused");
    bad.order = MARKOV_MAX_ORDER + 1;
    check(markov_build(&c, 1, &bad) == nullptr, "order above the maximum is refused");
    bad = cfg();
    bad.max_words = 0;
    check(markov_build(&c, 1, &bad) == nullptr, "max_words 0 is refused");
    auto ok = cfg();
    check(markov_build(nullptr, 1, &ok) == nullptr, "null parts is refused");
    check(markov_build(&c, 0, &ok) == nullptr, "zero parts is refused");
    check(markov_build(&c, 1, nullptr) == nullptr, "null config is refused");
  }

  // ---- degenerate corpora fail, they do not crash --------------------------
  {
    auto e = cfg();
    std::string empty;
    auto p0 = part(empty);
    check(markov_build(&p0, 1, &e) == nullptr, "empty corpus fails cleanly");

    std::string blanks = "\n\n   \n\r\n";
    auto p1 = part(blanks);
    check(markov_build(&p1, 1, &e) == nullptr, "whitespace-only corpus fails cleanly");

    std::string nul_only(4, '\0');
    auto p2 = markov_corpus_t{nul_only.data(), nul_only.size()};
    check(markov_build(&p2, 1, &e) == nullptr, "NUL-only corpus fails cleanly");

    markov_corpus_t nullpart{nullptr, 0};
    check(markov_build(&nullpart, 1, &e) == nullptr, "a null piece is skipped, not dereferenced");
  }

  // ---- the trailing NUL is a separator, never a word -----------------------
  // EMBED_TXTFILES appended one and it interned as a word, so ~2% of sentences
  // came out empty. A file read off LittleFS can carry one too.
  {
    std::string withnul = kCorpus;
    withnul.push_back('\0');
    auto p = markov_corpus_t{withnul.data(), withnul.size()};
    auto c = cfg();
    markov_chain_t* ch = markov_build(&p, 1, &c);
    check(ch != nullptr, "corpus with a trailing NUL builds");
    if (ch) {
      int empties = 0;
      char out[160];
      for (int i = 0; i < 4000; i++)
        if (markov_generate(ch, out, sizeof out) == 0) empties++;
      check(empties == 0, "no empty sentence in 4000 generations");
      markov_free(ch);
    }
  }

  // ---- output buffer discipline -------------------------------------------
  {
    auto p = part(kCorpus);
    auto c = cfg();
    markov_chain_t* ch = markov_build(&p, 1, &c);
    check(ch != nullptr, "reference corpus builds");
    if (ch) {
      // Tiny buffers must not write past the end and must still terminate.
      for (size_t cap = 1; cap <= 12; cap++) {
        std::string buf(cap + 8, '\xAA');
        size_t n = markov_generate(ch, buf.data(), cap);
        check(n < cap, "length stays inside the buffer");
        check(buf[n] == '\0', "output is terminated at the returned length");
        for (size_t i = cap; i < buf.size(); i++)
          check(buf[i] == '\xAA', "nothing is written past cap");
      }
      // cap == 0 must not touch the buffer at all
      char guard = '\xAA';
      check(markov_generate(ch, &guard, 0) == 0, "cap 0 returns 0");
      check(guard == '\xAA', "cap 0 writes nothing");

      // Even a refused call terminates, so an ignoring caller cannot print junk.
      char small[2] = {'\xAA', '\xAA'};
      check(markov_generate(ch, small, 1) == 0, "cap 1 returns 0");
      check(small[0] == '\0', "cap 1 still terminates");

      check(markov_generate(nullptr, small, sizeof small) == 0, "null chain returns 0");
      markov_free(ch);
    }
  }

  // ---- max_words is honoured ----------------------------------------------
  {
    // A corpus that loops forever if nothing stops it.
    std::string loop = "a b a\n";
    auto p = part(loop);
    markov_cfg_t c = cfg(1);
    c.max_words = 5;
    markov_chain_t* ch = markov_build(&p, 1, &c);
    check(ch != nullptr, "cyclic corpus builds at order 1");
    if (ch) {
      char out[256];
      for (int i = 0; i < 2000; i++) {
        size_t n = markov_generate(ch, out, sizeof out);
        int words = n ? 1 : 0;
        for (size_t k = 0; k < n; k++)
          if (out[k] == ' ') words++;
        check(words <= 5, "never more than max_words words");
        if (words > 5) break;
      }
      markov_free(ch);
    }
  }

  // ---- every order in range works, and the key width does not leak ---------
  {
    for (uint8_t order = 1; order <= MARKOV_MAX_ORDER; order++) {
      auto p = part(kCorpus);
      auto c = cfg(order);
      markov_chain_t* ch = markov_build(&p, 1, &c);
      check(ch != nullptr, "builds at every supported order");
      if (ch) {
        char out[160];
        int produced = 0;
        for (int i = 0; i < 500; i++)
          if (markov_generate(ch, out, sizeof out) > 0) produced++;
        check(produced > 0, "generates something at every supported order");
        markov_free(ch);
      }
    }
  }

  // ---- the seed actually seeds --------------------------------------------
  // The spike used a fixed constant, so the buddy said the same sentences in
  // the same order after every power cycle.
  {
    auto p = part(kCorpus);
    auto a = cfg(2, 0, 1);
    auto b = cfg(2, 0, 999983);
    markov_chain_t* ca = markov_build(&p, 1, &a);
    markov_chain_t* cb = markov_build(&p, 1, &b);
    check(ca && cb, "both seeds build");
    if (ca && cb) {
      std::string sa, sb;
      char out[160];
      for (int i = 0; i < 20; i++) {
        markov_generate(ca, out, sizeof out); sa += out; sa += '|';
        markov_generate(cb, out, sizeof out); sb += out; sb += '|';
      }
      check(sa != sb, "different seeds give different sequences");
      markov_free(ca);
      markov_free(cb);
    }
    // Seed 0 must not freeze the generator on a constant.
    auto z = cfg(2, 0, 0);
    markov_chain_t* cz = markov_build(&p, 1, &z);
    check(cz != nullptr, "seed 0 still builds");
    if (cz) {
      std::set<std::string> seen;
      char out[160];
      for (int i = 0; i < 200; i++) { markov_generate(cz, out, sizeof out); seen.insert(out); }
      check(seen.size() > 1, "seed 0 does not produce one sentence forever");
      markov_free(cz);
    }
  }

  // ---- the byte budget is enforced before allocating -----------------------
  {
    auto p = part(kCorpus);
    auto tiny = cfg(2, 64);
    check(markov_build(&p, 1, &tiny) == nullptr, "a corpus over max_bytes is refused");
    auto roomy = cfg(2, 1u << 20);
    markov_chain_t* ch = markov_build(&p, 1, &roomy);
    check(ch != nullptr, "a corpus inside max_bytes builds");
    if (ch) {
      check(markov_bytes(ch) > 0, "reports its own size");
      check(markov_bytes(ch) <= (1u << 20), "reported size respects the budget");
      check(markov_lines(ch) == 6, "counts its lines");
      markov_free(ch);
    }
  }

  // ---- hostile input: long words, long lines, no newline, huge vocabulary ---
  // The spike aborted on the first of these and corrupted the heap on the last.
  {
    auto c = cfg();
    std::string longword(200000, 'x');
    longword += "\n";
    auto p = part(longword);
    markov_chain_t* ch = markov_build(&p, 1, &c);
    check(ch != nullptr, "a single 200 KB word builds instead of aborting");
    markov_free(ch);

    std::string noeol = "sin salto de linea al final";   // no trailing '\n'
    auto p2 = part(noeol);
    markov_chain_t* ch2 = markov_build(&p2, 1, &c);
    check(ch2 != nullptr, "a corpus with no trailing newline builds");
    if (ch2) {
      check(markov_lines(ch2) == 1, "the last line still counts");
      markov_free(ch2);
    }

    // Every word distinct: this is the shape that used to walk off the offset
    // table. 40k unique words is inside the 16-bit id space; 80k is not, and
    // must be refused rather than silently wrapped.
    std::string many;
    for (int i = 0; i < 40000; i++) many += "w" + std::to_string(i) + (i % 8 == 7 ? "\n" : " ");
    many += "\n";
    auto p3 = part(many);
    markov_chain_t* ch3 = markov_build(&p3, 1, &c);
    check(ch3 != nullptr, "40k distinct words build");
    markov_free(ch3);

    std::string toomany;
    for (int i = 0; i < 80000; i++) toomany += "w" + std::to_string(i) + (i % 8 == 7 ? "\n" : " ");
    toomany += "\n";
    auto p4 = part(toomany);
    check(markov_build(&p4, 1, &c) == nullptr, "a vocabulary past the id space is refused");
  }

  // ---- multi-part corpora: this is what a pool is -------------------------
  {
    std::string a = "hola que tal\nhola de nuevo\n";
    std::string b = "que tal todo\nde nuevo aqui\n";
    markov_corpus_t parts[2] = {part(a), part(b)};
    auto c = cfg();
    markov_chain_t* ch = markov_build(parts, 2, &c);
    check(ch != nullptr, "several pieces build as one chain");
    if (ch) {
      check(markov_lines(ch) == 4, "lines from every piece are counted");
      // Splicing across pieces is the whole point of a pool: more material
      // means more places to join. Measured novelty was 12.8% at 60 lines.
      std::set<std::string> seen;
      char out[160];
      for (int i = 0; i < 3000; i++) {
        if (markov_generate(ch, out, sizeof out) > 0) seen.insert(out);
      }
      check(seen.size() > 4, "generates sentences the corpus did not contain");
      markov_free(ch);
    }
  }

  // ---- UTF-8 survives: the corpus is Spanish -------------------------------
  {
    std::string es =
        "el ñandú corrió más rápido\n"
        "más rápido que ayer\n"
        "corrió más lejos que el ñandú\n";
    auto p = part(es);
    auto c = cfg();
    markov_chain_t* ch = markov_build(&p, 1, &c);
    check(ch != nullptr, "accented corpus builds");
    if (ch) {
      char out[160];
      for (int i = 0; i < 2000; i++) {
        size_t n = markov_generate(ch, out, sizeof out);
        // No continuation byte may start a word, and the string must be
        // well-formed UTF-8 -- a split multi-byte character would show here.
        for (size_t k = 0; k < n; k++) {
          unsigned char ch0 = (unsigned char)out[k];
          if (k == 0 || out[k - 1] == ' ')
            check((ch0 & 0xC0) != 0x80, "no word starts mid-character");
        }
      }
      markov_free(ch);
    }
  }

  // ---- free is total and null-safe ----------------------------------------
  markov_free(nullptr);

  std::printf("%s: %d checks, %d failures\n", g_fail ? "FAILED" : "ok", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
