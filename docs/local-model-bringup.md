# Local model bring-up — Step 0: prove the inference path

**Status:** DONE — run 2026-08-05. Verdict: **yellow, with headroom.**
Code and full notes: branch `spike/tinylm-s3` (spikes are not merged to main).
**Audience:** whoever (human or agent) picks this up before Workshop 0.

## What this is, and what it is not

We want the buddy to generate its own short in-character utterances on-device,
from a small model we train ourselves (see "Where this leads" at the bottom).

Before we spend money and a weekend training anything, we need to answer one
question with a **measured number**, not an estimate:

> Can an ESP32-S3 run transformer inference fast enough, *while the face is
> still animating*, to be worth doing at all?

This document is only about answering that. It uses a throwaway model.

### The throwaway model: `stories260K`

**`stories260K` is NOT the model from
[slvDev/esp32-ai](https://github.com/slvDev/esp32-ai), and it is not the model
we intend to ship.** Three different things, easy to confuse:

| | What | Why it's here |
|---|---|---|
| **slvDev/esp32-ai** | 28.9M params, 4-bit, **14.9 MB** | The project that inspired this. **Rejected**: does not fit our flash (see `docs/ideas-exploration.html`). Good source of technique. |
| **`stories260K`** | ~260K params, fp32, **~1 MB** | **This document.** A disposable benchmark. Its output is near-gibberish. We do not care what it says — only how fast it says it. |
| **"Buddy voice" model** | ~1.3–4M params, 4-bit, ~0.7–2 MB | What we actually want to train. Blocked on the answer here. |

`stories260K` is from Andrej Karpathy's [llama2.c](https://github.com/karpathy/llama2.c).
It is the right benchmark because it is *deliberately tiny*: same architecture
family as what we'd train, small enough to run in fp32 with **no quantization
work at all**, and its inference engine (`run.c`) is a single dependency-free
C file under 1000 lines. That means Step 0 is a porting exercise, not a
research project. If we had to write a 4-bit kernel first, this would be a
month instead of a weekend.

## Prerequisites

- ESP32-S3 N16R8 board with the round display already working (`hardware/buddy-s3-display.md`)
- ESP-IDF v6.0.2 (`source ~/.espressif/tools/activate_idf_v6.0.2.sh`)
- The `model` partition, already added to `firmware/partitions.csv` (4 MB, raw, offset 0x710000)

## Step 1 — Get the model and verify its shape

```bash
mkdir -p /tmp/l2c && cd /tmp/l2c
git clone --depth 1 https://github.com/karpathy/llama2.c
ls -la llama2.c/stories260K/    # stories260K.bin + tok512.bin
```

If that folder is missing, the models are also mirrored at
`huggingface.co/karpathy/tinyllamas`. **Do not trust any config numbers quoted
from memory — read them out of the file.** The legacy llama2.c header is seven
little-endian int32s:

```python
import struct
with open('llama2.c/stories260K/stories260K.bin','rb') as f:
    dim, hidden_dim, n_layers, n_heads, n_kv_heads, vocab_size, seq_len = \
        struct.unpack('<7i', f.read(28))
print(f"{dim=} {hidden_dim=} {n_layers=} {n_heads=} {n_kv_heads=} {vocab_size=} {seq_len=}")
# A negative vocab_size means the classifier weights are NOT shared with the
# embedding table — run.c keys off this, so note which one you have.
```

Record these numbers in this file when you get them. Everything downstream
(memory sizing, the extrapolation in Step 5) depends on them.

## Step 2 — Flash the weights into the `model` partition

The weights go in **raw**, not as a file, so they can be memory-mapped at all —
`esp_partition_mmap` works on a raw partition but not on a file inside LittleFS.
(Step 4 found that we should *not* actually run from the mapping: copy it into
PSRAM at boot instead. The raw partition is still the right place to store it.)

```bash
esptool.py --chip esp32s3 write_flash 0x710000 llama2.c/stories260K/stories260K.bin
```

Confirm the offset against `firmware/partitions.csv` before running this —
writing to the wrong offset will corrupt the app or the pack filesystem.

The tokenizer (`tok512.bin`, a few KB) can go in LittleFS as a normal file;
it's read sequentially once at startup, so it doesn't need mmap.

## Step 3 — Port `run.c` into an ESP-IDF component

Create `firmware/components/tinylm/`. Copy `llama2.c/run.c` in as-is, then make
exactly four changes. Resist doing more — the goal is a measurement, not a
good component.

1. **Weights: file I/O → mmap.** Replace the `mmap()`/`open()` block in
   `build_transformer` with:
   ```c
   const esp_partition_t* p = esp_partition_find_first(
       ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "model");
   const void* base = NULL;
   esp_partition_mmap_handle_t h;
   ESP_ERROR_CHECK(esp_partition_mmap(p, 0, p->size,
                                      ESP_PARTITION_MMAP_DATA, &base, &h));
   ```
   then point the weight pointers into `base` exactly as the original code
   points them into the mmap'd file.
2. **RunState buffers → PSRAM.** Every `calloc` in `malloc_run_state` becomes
   `heap_caps_calloc(..., MALLOC_CAP_SPIRAM)`. These are the activations and
   KV cache — they're written every token, so they must be in RAM, not flash.
3. **Tokenizer from an embedded array**, not a file — a benchmark should not
   need a filesystem. And **drop `main()`**.
4. **Add a `CMakeLists.txt`**: `idf_component_register(SRCS "run.c" INCLUDE_DIRS "include" REQUIRES spi_flash esp_partition)`
   and set `-ffast-math -O2` on this component only.

Then call it from a task pinned to **core 1**, priority **3** (below the face
task at 4, so the face wins contention — we want to measure the face
degrading, not starving).

## Step 4 — Measure

Take five numbers. Log them here in the table below.

| Metric | Result |
|---|---|
| Model, read from its own header | dim 64, hidden 172, 5 layers, 8 heads, 4 kv-heads, vocab 512, seq 512 |
| Parameters | 260,672 total — **227,840 non-embedding** (the part that costs compute) |
| tok/s, weights memory-mapped from flash | **12.6** (79.5 ms/token) |
| tok/s, weights copied to PSRAM | **31.4** (31.8 ms/token) |
| Per-token p50 / p99 (PSRAM) | 31.7 / 39.2 ms — very flat, no jitter problem |
| PSRAM consumed | 676 KB, almost all of it the KV cache |
| Internal RAM consumed | ~17 KB |
| Output | coherent TinyStories prose — the port is correct, not just fast |

### The headline finding: do NOT memory-map the weights

Every token reads the whole weight set, and 1 MB does not fit in the MMU
cache — so mmap'd inference spends **60% of its time waiting on flash**.
Copying the weights into PSRAM at boot is **2.5x faster**, and it is free for
us: PSRAM is 8 MB and every model we are considering is 0.7–3.4 MB.

This is where the esp32-ai project's Per-Layer Embeddings trick stops applying
to us. PLE exists because a 14.9 MB model *cannot* fit in RAM. Ours fits, so we
simply keep them resident and skip the whole problem.

## Step 5 — Extrapolate to the real model

Compute cost scales with **non-embedding** parameters (the embedding table is
a lookup, not a matmul). For `stories260K`, non-embedding ≈
`n_layers × 12 × dim²`. For our candidate Config S (vocab 1024, d=128,
6 layers) that's ~1.18M.

```
predicted_ms_per_token(Config S) ≈ measured_ms_per_token(260K) × (1.18M / non_emb_260K)
```

Then apply a **0.5–0.7× speedup factor** for int4 weights (less memory traffic,
more unpack cost) — and treat that factor as a guess until someone measures it.

### Decision gates — and where we landed

Scaling by non-embedding parameters from the PSRAM number:

| | ms/token | fp32 | int4 (est. 0.6x) |
|---|---|---|---|
| **Config S** (v1024 d128 L6, 1.18 M non-emb) | 164.8 | 6.1 tok/s | **~10 tok/s** |
| Config M (v2048 d192 L8, 3.54 M) | 494 | 2.0 tok/s | ~3.4 tok/s |
| Config L (v2048 d256 L8, 6.29 M) | 878 | 1.1 tok/s | ~1.9 tok/s |

Against the gates: **Config S is YELLOW** (5–15 tok/s). Good for short quips
and idle murmurs; not conversational. Config M and L are red.

**But three levers are completely untouched**, and two are large:

1. **`seq_len` is 512 and we need ~64.** A 15-word utterance is ~20 tokens.
   That cuts the KV cache 8x (676 KB → ~85 KB) *and* cuts attention compute.
2. **The matmul is a naive scalar triple loop.** The S3 has 128-bit SIMD and
   ESP-DSP ships optimised dot products. For scale: esp32-ai runs ~17x more
   non-embedding parameters at a similar token rate, which implies its kernel
   is roughly an order of magnitude better than llama2.c's reference C.
3. **int4** — the 0.6x above is a guess, not a measurement.

So the honest reading is **not "the chip can't"** — a naive, unvectorised fp32
port already gets Config S into the usable band. The remaining question is how
much kernel work the lab wants to do, and esp32-ai is open source to learn from.

### The face-contention question answered differently

The original plan was to measure inference against a busy face. That framing is
now obsolete: since the eyes were cached, the face renders only on a blink or
emotion change and blits otherwise — it is **idle most of the time**. Per-token
jitter here is 31.7 p50 vs 39.2 p99 with nothing else running; the face's
occasional ~8 ms draw plus 23 ms push will not meaningfully disturb that.
The contention worry was a product of the *old* renderer's cost.

## Known risks

- **PSRAM bandwidth contention.** The framebuffer (112 KB) also lives in PSRAM
  and is DMA'd to the display every frame. Inference activations compete for
  the same bus. This may hurt more than CPU time does, and it will not show up
  in a CPU-only benchmark — which is exactly why row 2 of the table exists.
- **`esp_partition_mmap` address space is finite.** The S3 has a limited number
  of MMU pages for data mapping. A 1 MB mapping is fine; a 4 MB one may not be.
  Worth knowing before the real model gets bigger.
- **fp32 is not what we'll ship.** Step 0 deliberately skips quantization.
  Expect the int4 numbers to differ; that's what the fudge factor in Step 5 is for.

## Where this leads (not part of Step 0)

If the gates pass: generate ~100k mood-labelled utterances with Claude Haiku
(~$15), train a custom 1024-token BPE tokenizer on that corpus, train Config S
with `nanoGPT` or `llama2.c`'s trainer, quantize to int4, and ship it as the
offline Brain path. The corpus is the personality — which is why the **corpus
format, the affect-token set, and the expression vocabulary are group
decisions**, not something to settle here. See `docs/ideas-exploration.html`.
