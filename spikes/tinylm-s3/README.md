# Spike: transformer inference on the ESP32-S3 (Step 0)

Answers the gate from [`docs/local-model-bringup.md`](../../docs/local-model-bringup.md):
**can this chip run a language model fast enough to be worth training one?**

`stories260K` is a throwaway benchmark, not a candidate. Its output is
children's-story filler and we do not care what it says — only how fast.

## Result: yellow, with a lot of headroom

```
weights mmap'd from flash    12.6 tok/s   79.5 ms/token
weights copied to PSRAM      31.4 tok/s   31.8 ms/token   <- 2.5x
per-token p50 / p99          31.7 / 39.2 ms  (flat; jitter is not a problem)
PSRAM used                   676 KB (almost entirely KV cache)
internal RAM used            ~17 KB
```

### Do not memory-map the weights

Every token reads the whole weight set, and 1 MB does not fit in the MMU cache,
so mmap'd inference spends **~60% of its time waiting on flash**. Keep the raw
partition as *storage*, but `memcpy` it into PSRAM at boot. It costs 1 MB of an
8 MB part and buys 2.5x.

This is also where esp32-ai's Per-Layer Embeddings trick stops being relevant
to us: PLE exists because a 14.9 MB model cannot fit in RAM. Ours fits.

### Extrapolation

Scaled by non-embedding parameters (the embedding table is a lookup, not a
matmul — 227,840 of stories260K's 260,672 params are non-embedding):

| | fp32 | int4 (est.) |
|---|---|---|
| **Config S** (v1024 d128 L6) | 6.1 tok/s | **~10 tok/s** |
| Config M (v2048 d192 L8) | 2.0 tok/s | ~3.4 tok/s |
| Config L (v2048 d256 L8) | 1.1 tok/s | ~1.9 tok/s |

**Config S is usable. M and L are not**, on this implementation.

### Three levers nobody has pulled yet

1. **`seq_len` is 512; we need ~64.** A 15-word utterance is ~20 tokens. Cuts
   the KV cache 8x (676 KB → ~85 KB) *and* cuts attention compute.
2. **The matmul is a naive scalar triple loop.** The S3 has 128-bit SIMD and
   ESP-DSP ships optimised dot products. esp32-ai runs ~17x more non-embedding
   parameters at a similar token rate — its kernel is roughly an order of
   magnitude better than llama2.c's reference C.
3. **int4** — the 0.6x factor above is a guess, not a measurement.

So this is **not** "the chip can't". An unvectorised fp32 port already puts
Config S in the usable band.

## Step 0.5: the kernel optimisation pass

Brief: [`OPTIMIZATION-BRIEF.md`](OPTIMIZATION-BRIEF.md). Target was **≥3×**
over the 31.4 tok/s fp32-PSRAM baseline. Landed:

| variant | tok/s | ms/token | p50 / p99 | vs baseline | coherent? |
|---|---|---|---|---|---|
| fp32, mmap'd flash | 13.3 | 75.0 | 75.0 / 79.1 | 1.06× | yes |
| fp32, PSRAM (reference) | 38.0 | 26.3 | 26.3 / 30.5 | 1.21× | yes, word-identical |
| int8, weights in PSRAM | 87.8 | 11.4 | 11.3 / 15.6 | 2.80× | yes |
| **int8, weights in internal RAM** | **152.4** | **6.56** | 6.58 / 10.41 | **4.85×** | yes |
| int8 internal, seq_len 128 | 151.7 | 6.59 | 6.59 / 10.40 | 4.83× | yes, identical |

Every pass runs the same 120-token generation with the same RNG seed, so the
sample texts are directly comparable. p99 stayed flat throughout (~1.6× p50) —
jitter is still not a problem. Weights: 277,536 B int8 (fits internal RAM
with 89 KB to spare); PSRAM high-water unchanged except where noted.

### Where the 4.85× came from (each commit carries its own before/after)

1. **Profile first** (`prof.h`, cycle-accurate buckets, zero overhead): every
   matmul ran at ~19.5 cycles/MAC *regardless of shape* — memory-latency-bound
   on scalar 4-byte PSRAM reads, not compute-bound.
2. **ESP-DSP SIMD matmul: +3.8% only** — and that *confirmed* the diagnosis:
   wider loads don't fix cache-miss stalls. Kept for the alignment
   infrastructure the int8 path needs.
3. **int8 row quantisation (+ per-row scales): 31.4 → 56.8 tok/s.** 4× less
   traffic, and `dsps_dp_s8_aes3` does 16 MACs per instruction. Not upstream
   `runq.c`'s format: flat Q8_0 grouping degrades to group-size 4 on
   stories260K (hidden 172 = 4×43); per-row scales with rows padded to 16 B
   match the SIMD contract instead. Quality cost: invisible (samples stay
   coherent stories; max per-weight error 0.007).
4. **Same weights in internal RAM: 56.8 → 78.4.** Placement is the whole
   diff — the outputs are byte-identical.
5. **Fast `expf`: 78.4 → 114.0.** newlib's is ~420 cycles; softmax + SwiGLU +
   the sampler call it thousands of times per token. A 15-cycle 2^x
   construction (rel err < 2.5e-4) left the fp32 sample **word-identical**.
6. **RoPE hoist + magic-constant rounding: 114 → 141.** The angles only
   depend on `(pos, pair)` but were recomputed per layer; `lrintf` was a
   libcall in the quantiser.
7. **`-funroll-loops`: 141 → 152.** Unrolls the `head_size=8` attention
   loops, the hot scalar code once matmul went to SIMD.

Measured and rejected (so nobody re-tries them): hoisting `sqrtf` out of the
score loop and softmax divide→reciprocal — zero change, `-ffast-math` already
does both. `seq_len` 512→128: zero time, but **+504 KB of free PSRAM** (KV
cache 655→164 KB); it's a memory lever, not a speed lever, because per-token
KV traffic scales with the *position*, not the allocation.

Not attempted: dual-core. The remaining profile caps it at ~1.6–1.8× more,
but on the real device core 0 runs WiFi/TLS and core 1 runs the face, so the
number could not inform the product decision. Next kernel levers if ever
needed: int4 weights (halves PSRAM traffic, fits more of Config S internal),
int8 KV cache (attention is now 64% of the token and is KV-traffic-bound).

### Profile, before → after (per token, int8-internal at the end)

```
matmuls (all)        21.12 ms  69%  →  1.16 ms  19%
attention             7.22 ms  23%  →  3.94 ms  64%
SwiGLU                1.56 ms   5%  →  0.67 ms  11%
RoPE                  0.61 ms   2%  →  0.05 ms   1%
quantise x                 —        →  0.19 ms   3%
rmsnorm+residuals     0.22 ms   1%  →  0.13 ms   2%
TOTAL forward()      30.73 ms       →  6.14 ms
```

### Two hardware traps, documented in code but repeated here

- **The 28-byte checkpoint header leaves every tensor at +12 mod 16.**
  `dsps_dotprod_f32_aes3` checks alignment and *silently* falls back to a
  scalar-load body — the "optimisation" measures as noise unless the blob is
  landed 4 bytes into a 16-byte-aligned allocation.
- **`dsps_dp_s8_aes3` over-reads 16 bytes past both operands** (pipelined
  `ee.vld.128`). Every buffer it touches needs 16 B of slack, or the crash is
  rare, allocation-order-dependent, and miserable to find.

### A 240 MHz: el reloj solo ayuda a lo que es cómputo

Todo lo de arriba se midió a **160 MHz**, que es el defecto de ESP-IDF; el S3
llega a 240. Repetido a 240 MHz (ratio de reloj: 1,5×):

| variante | 160 MHz | 240 MHz | factor |
|---|---|---|---|
| fp32, mmap desde flash | 13,3 | 13,9 tok/s | **1,05×** |
| fp32, en PSRAM | 38,0 | 42,7 tok/s | 1,12× |
| int8, en PSRAM | 87,8 | 105,5 tok/s | 1,20× |
| **int8, en RAM interna** | 152,4 | **208,6 tok/s** | **1,37×** |

**El gradiente es la confirmación del diagnóstico.** Cuanto más limitada está
una variante por la latencia de memoria, menos gana con el reloj: la latencia
es fija en nanosegundos, así que a 240 MHz una espera cuesta 1,5× más ciclos y
el mismo tiempo real. La variante mmap, que espera a la flash, se queda en
1,05×; la que vive en RAM interna, que es cómputo casi puro, llega a 1,37×.

Es el mismo hallazgo que el perfil original (19,5 ciclos/MAC, limitado por
latencia) visto desde el otro lado.

**Para Config S cambia poco, y es lo que hay que contarle al grupo.** Config S
no cabe en RAM interna, así que corre desde PSRAM, que es justo la fila que
menos gana. Escalando la fila int8-PSRAM por parámetros no-embedding:
**~20 tok/s** (antes ~17), que pone una frase de 15 palabras en **~0,98 s** —
por debajo del segundo, pero por poco.

Aun así, subir el reloj es gratis y ayuda a todo lo demás del firmware (el
handshake TLS baja de 2726 a 1851 ms, medido). Lo que no hace es rescatar un
modelo limitado por memoria.

### Updated extrapolation, and the recommendation

Config S (1,179,648 non-emb params) at int8 is ~1.2 MB — too big for internal
RAM, so it runs from PSRAM. Scaling the measured int8-PSRAM pass per-param
gives **59 ms/token = 17 tok/s** (conservative: attention and the sampler
scale slower than params; the structure-aware estimate is ~40 ms ≈ 25 tok/s).
A 15-word line (~20 tokens) is therefore **0.8–1.2 s end to end**, and a
partial-internal placement (~380 KB of the hottest tensors) or int4 pushes it
toward ~0.6–0.9 s. The brief's "needs ~2 s, too slow to feel alive" problem
is gone.

**Recommendation: the kernel is no longer the reason to say no.** Before this
pass, the per-parameter efficiency gap to esp32-ai was 5.4×; it is now ~1.1×
(34.7M vs 38.6M param·tok/s), with their remainder being int4. Two honest
caveats for the group:
- These numbers are stories260K's *speed* wearing Config S's parameter count.
  Whether a 1.3M-param model can be *worth listening to* is a training
  question this spike cannot answer — the line-bank alternative stays on the
  table.
- The int8-internal 152 tok/s headline does not transfer to Config S (it
  doesn't fit internal); the transferable number is the PSRAM row.

## Running it

```bash
# 1. weights (not vendored — 1 MB binary)
curl -LO https://huggingface.co/karpathy/tinyllamas/resolve/main/stories260K/stories260K.bin
esptool.py --chip esp32s3 -p PORT write-flash 0x710000 stories260K.bin

# 1b. int8 blob for the quantised passes (needs only stdlib Python)
python3 tools/quantize_rowq8.py stories260K.bin stories260K-rowq8.bin
esptool.py --chip esp32s3 -p PORT write-flash 0x910000 stories260K-rowq8.bin

# 2. the benchmark
idf.py set-target esp32s3 && idf.py -p PORT flash monitor
```

`main/run.c` is Karpathy's [llama2.c](https://github.com/karpathy/llama2.c)
`run.c` with four changes: weights from `esp_partition_mmap`, activations into
PSRAM, tokenizer read from an embedded array (`tok512.h`) instead of a file,
and no `main()`. `relocate_weights_to_psram()` is the addition that produced
the headline finding.

The partition table is copied verbatim from `firmware/partitions.csv` so the
`model` offset matches the real device.
