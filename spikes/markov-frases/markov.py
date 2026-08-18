#!/usr/bin/env python3
"""Markov-chain sentence generation over a register-labelled corpus.

Normal use — expand the group's corpus and emit ONLY new lines, for a human
to review before any of them reach the buddy:

    python3 markov.py corpus.txt --order 2 --new 50 > proposals.txt

Diagnostics — is the corpus dense enough to recombine at all?

    python3 markov.py corpus.txt --measure

On-device cost — what a C port would need on the ESP32-S3:

    python3 markov.py corpus.txt --memory

Corpus format is `register: sentence`, one per line; `#` lines are ignored.
The corpus itself is Spanish (it is the group's data); the code is English,
matching the rest of the project.

The tension that decides everything: raising the order yields more
grammatical output but converges on replaying the corpus verbatim; lowering
it yields real novelty but breaks Spanish agreement.
"""
import argparse, collections, random, sys

BEGIN, END = "\x02", "\x03"


def load(path):
    registers = collections.defaultdict(list)
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#") or ":" not in line:
            continue
        reg, sentence = line.split(":", 1)
        registers[reg.strip()].append(sentence.strip())
    return registers


def build(sentences, order):
    """state (order words) -> list of words that may follow it."""
    table = collections.defaultdict(list)
    for s in sentences:
        toks = [BEGIN] * order + s.split() + [END]
        for i in range(len(toks) - order):
            table[tuple(toks[i:i + order])].append(toks[i + order])
    return table


def generate(table, order, limit=40):
    state, out = (BEGIN,) * order, []
    for _ in range(limit):
        options = table.get(state)
        if not options:
            break
        word = random.choice(options)   # repetition IS the weighting
        if word == END:
            break
        out.append(word)
        state = (state + (word,))[1:]
    return " ".join(out)


def measure(registers, tries=200):
    print(f"{'order':>6} {'register':>12} {'verbatim copies':>17} {'new':>7}")
    for order in (1, 2, 3):
        for reg, sentences in registers.items():
            known = set(sentences)
            table = build(sentences, order)
            outs = [g for g in (generate(table, order) for _ in range(tries)) if g]
            copies = sum(1 for o in outs if o in known)
            fresh = {o for o in outs if o not in known}
            print(f"{order:>6} {reg:>12} {copies / len(outs) * 100:>16.0f}% {len(fresh):>7}")
        print()


def propose(registers, order, count, max_tries=20000):
    """Only sentences NOT already in the corpus. Output is for human review."""
    for reg, sentences in registers.items():
        known = set(sentences)
        table = build(sentences, order)
        found, tries = [], 0
        while len(found) < count and tries < max_tries:
            g = generate(table, order)
            tries += 1
            if g and g not in known and g not in found:
                found.append(g)
        print(f"# {reg} — {len(found)} proposals (order {order})")
        for g in found:
            print(f"{reg}: {g}")
        if len(found) < count:
            print(f"#   (corpus for '{reg}' is exhausted; write more lines "
                  f"or lower the order)")
        print()


# --- on-device cost model --------------------------------------------------
# Layout a C port would use, chosen so lookup is a binary search with no
# pointer chasing (pointers would triple the size and scatter PSRAM reads):
#
#   vocab blob   : unique words, NUL-separated, kept in flash (read rarely)
#   vocab offsets: uint32 per unique word
#   transitions  : sorted array of (order+1) uint16 word ids, 2*(order+1) B
#                  each. Binary search finds the first row matching the
#                  state; the matching rows are contiguous, so picking a
#                  successor is one scan over a handful of entries.
#
# Timing is anchored to a number THIS project measured, not a guess: in
# spikes/tinylm-s3 a random 4-byte PSRAM read cost ~20 cycles (the matmul ran
# at ~19.5 cycles/MAC, latency-bound on exactly such reads). At 240 MHz that
# is ~83 ns per random read, and a binary search over N rows costs log2(N)
# of them.
PSRAM_READ_NS = 83.0


def memory(registers, order):
    import math
    words, lines = [], 0
    for sentences in registers.values():
        for s in sentences:
            words += s.split()
            lines += 1
    vocab = sorted(set(words))
    blob = sum(len(w.encode()) + 1 for w in vocab)
    # one transition per (state -> next), including the END of every sentence
    transitions = len(words) + lines
    rec = 2 * (order + 1)
    table_b = transitions * rec
    offsets_b = len(vocab) * 4
    steps = math.log2(transitions) if transitions > 1 else 1
    per_word_us = steps * PSRAM_READ_NS / 1000.0
    print(f"corpus            {lines} lines, {len(words)} words, "
          f"{len(vocab)} unique")
    print(f"order             {order}  ({rec} B per transition row)\n")
    print(f"transition table  {table_b / 1024:8.1f} KB   <- RAM (PSRAM)")
    print(f"vocab offsets     {offsets_b / 1024:8.1f} KB   <- RAM (PSRAM)")
    print(f"vocab text        {blob / 1024:8.1f} KB   <- can stay in flash")
    print(f"RAM total         {(table_b + offsets_b) / 1024:8.1f} KB\n")
    print(f"lookup            {steps:.0f} binary-search steps = "
          f"{per_word_us:.2f} us/word")
    print(f"15-word sentence  {per_word_us * 20:.0f} us "
          f"(~20 word-pieces)\n")

    # What the group actually asks: how does this scale to a real corpus?
    print("projection to a bigger corpus (~8 words/line, order "
          f"{order}, {rec} B/row):")
    print(f"  {'lines':>8} {'RAM':>9} {'lookup':>9} {'sentence':>10}")
    for n in (1_000, 5_000, 20_000, 100_000):
        tr = n * 9
        vocab_n = min(int(700 * (n * 8) ** 0.5 / 30), n * 8)   # Heaps' law, rough
        ram = tr * rec + vocab_n * 4
        st = math.log2(tr)
        us = st * PSRAM_READ_NS / 1000.0
        print(f"  {n:>8,} {ram / 1024:>7.0f} KB {us:>7.2f} us {us * 20:>8.0f} us")
    print("\nFor comparison, the neural model on the same board needs "
          "~800-1200 ms per\nsentence and ~1.2 MB of weights. Markov is "
          "~4-5 orders of magnitude faster\nand its cost is set by the "
          "corpus, not by a model.")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("corpus")
    p.add_argument("--order", type=int, default=2, help="words of context (1-3)")
    p.add_argument("--new", type=int, default=20, help="proposals per register")
    p.add_argument("--measure", action="store_true", help="copies/novelty table")
    p.add_argument("--memory", action="store_true", help="ESP32-S3 cost estimate")
    p.add_argument("--seed", type=int, default=7)
    a = p.parse_args()

    random.seed(a.seed)
    regs = load(a.corpus)
    if not regs:
        sys.exit(f"{a.corpus}: no lines matching 'register: sentence'")
    total = sum(len(v) for v in regs.values())
    nwords = sum(len(s.split()) for v in regs.values() for s in v)
    print(f"# corpus: {total} lines, {len(regs)} registers, {nwords} words\n")
    if a.measure:
        measure(regs)
    elif a.memory:
        memory(regs, a.order)
    else:
        propose(regs, a.order, a.new)
