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

## Running it

```bash
# 1. weights (not vendored — 1 MB binary)
curl -LO https://huggingface.co/karpathy/tinyllamas/resolve/main/stories260K/stories260K.bin
esptool.py --chip esp32s3 -p PORT write-flash 0x710000 stories260K.bin

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
