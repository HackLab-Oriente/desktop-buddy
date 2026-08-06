# Task brief: optimise transformer inference on the ESP32-S3

You are picking up a finished, working benchmark and making it **faster**. The
port already runs and produces correct output. Nothing here is exploratory —
this is an optimisation task with a measured baseline and a specific target.

Read this whole document before writing code. It contains the baseline, the
traps, and an explicit list of what *not* to do.

---

## 1. The goal in one paragraph

We want a desktop robot to generate its own short spoken lines locally, with no
API key and no network. A ~1.3M-parameter model ("Config S") would do the job,
but at today's speed it needs **~2 seconds** per 15-word line, which is too slow
to feel alive. The inference maths is an unvectorised scalar loop on a chip with
a 128-bit SIMD unit, so there is a large, well-understood gap between what we
get and what the hardware can do. **Close that gap.**

**Target: ≥3× faster tokens/second on the benchmark below.** 4–5× is plausible.
Below 2× the whole local-model direction probably gets dropped, so an honest
"I could only get 1.6× and here is why" is a genuinely useful result — do not
inflate a number to hit the target.

---

## 2. Where things are

**Branch: `spike/tinylm-s3`.** Work there and stay there. Do not merge to
`main`; in this repo spikes live on branches and only their *findings* are
merged, as a `docs/` update.

```
spikes/tinylm-s3/
  main/run.c        Karpathy's llama2.c run.c, ported. matmul() is at ~line 226.
  main/main.cpp     The benchmark harness. Prints tok/s, p50, p99, memory.
  main/run_api.h    Declarations the harness needs.
  main/tok512.h     Tokenizer, embedded as a byte array.
  partitions.csv    Copied from firmware/ so the model offset matches.
```

Related reading, in this order:
- `spikes/tinylm-s3/README.md` — the result this task builds on.
- `docs/local-model-bringup.md` (on `main`) — why any of this exists.
- `.kiro/steering/tech.md` — project-wide conventions you must follow.

### Hardware and toolchain

- **ESP32-S3 N16R8** — 16 MB flash, 8 MB octal PSRAM, ~380 KB free internal RAM.
- **ESP-IDF v6.0.2.** v5.x is not supported. Activate with
  `source ~/.espressif/tools/activate_idf_v6.0.2.sh`.
- The board is on a USB serial port whose name changes between reboots and
  between its two USB sockets. **Discover it** (`ls /dev/cu.usbmodem*`) and
  always pass `-p <port>` — a bare `idf.py flash` can hit the wrong board.

### Getting it running

```bash
# 1. weights — 1 MB, not vendored
curl -LO https://huggingface.co/karpathy/tinyllamas/resolve/main/stories260K/stories260K.bin
esptool.py --chip esp32s3 -p <PORT> write-flash 0x710000 stories260K.bin

# 2. build and run
cd spikes/tinylm-s3
idf.py set-target esp32s3
idf.py -p <PORT> build flash monitor
```

The weights only need flashing once; they survive app reflashes.

---

## 3. The baseline you must beat

Model, read from its own header — **do not trust numbers quoted from memory,
including these; re-measure before you start**:

```
dim 64 · hidden_dim 172 · 5 layers · 8 heads · 4 kv-heads · vocab 512 · seq_len 512
260,672 parameters total, of which 227,840 are non-embedding
```

Measured on hardware, fp32, weights resident in PSRAM:

```
throughput    31.4 tok/s      31.8 ms/token
per-token     p50 31.7 ms   p99 39.2 ms   (very flat — jitter is not a problem)
PSRAM         676 KB, almost entirely KV cache
internal RAM  ~17 KB
```

One earlier finding you must not undo: **running from memory-mapped flash is
2.5× slower** (12.6 tok/s) because every token reads the whole weight set and
1 MB does not fit in the MMU cache. `relocate_weights_to_psram()` exists for
this reason. Keep weights resident in RAM.

### Where the target comes from

A comparable project ([slvDev/esp32-ai](https://github.com/slvDev/esp32-ai))
runs ~3.9M non-embedding parameters at ~9.9 tok/s — about **5.4× more efficient
per parameter** than this code, using int4 weights and an optimised kernel. Some
of that 5.4× is quantisation and some is the kernel. 3× is a conservative ask.

---

## 4. Step one, before any optimisation: profile

**Do not skip this and do not guess where the time goes.** This project has
already been burned twice by optimising against an assumption:

- a graphics benchmark measured `fillScreen` and concluded "we are bandwidth-
  bound", when the real renderer was 4× more expensive and CPU-bound;
- this very benchmark was assumed to be compute-bound until someone tested the
  flash-vs-PSRAM hypothesis and found 60% of the time was memory stalls.

So: instrument `forward()` and report the millisecond split across **matmul,
attention (the softmax/score loops), rmsnorm, RoPE, and the final classifier**.
`esp_timer_get_time()` is fine. Accumulate per-token, print the breakdown.

`matmul()` carries a comment claiming it dominates. That is probably true here
too, but **prove it on this chip with these dimensions** — `dim` is only 64,
which is small enough that per-call overhead and the non-matmul parts may
matter more than they would at LLM scale. Report the split before changing
anything.

---

## 5. Optimisation avenues, ranked

Ranked by expected payoff per unit of risk. You are not required to do all of
them; you are required to measure each one you try.

**1. ESP-DSP for the dot product (highest leverage, lowest risk).**
Espressif ships [`espressif/esp-dsp`](https://components.espressif.com/components/espressif/esp-dsp)
as a managed component with SIMD-optimised primitives — `dsps_dotprod_f32` and
friends, with ESP32-S3-specific variants. The inner loop of `matmul()` is
exactly a dot product. Add the dependency to `main/idf_component.yml` and
replace the loop. Watch alignment: the S3 vector unit wants 16-byte-aligned
operands, and a misaligned pointer silently falls back to a slow path or
faults. `heap_caps_aligned_alloc` is your friend.

**2. int8 quantisation — and upstream already wrote it.**
`llama2.c` ships **`runq.c`**, a complete Q8_0 (int8, group-quantised)
implementation of the same model, with an `export.py` that produces the
quantised checkpoint. This is a port, not a research project. Two wins at once:
~4× less memory traffic, and integer SIMD is wider than float on this chip.
Do this *after* the profiling step so you can attribute the gain.

**3. Small model + int8 fits in INTERNAL RAM, which is ~2× faster than PSRAM.**
stories260K at int8 is ~260 KB, and there is ~380 KB of internal RAM free.
Given that this workload was already shown to be memory-stall-sensitive, this
may be one of the largest single wins available and it is cheap to test. It
will *not* generalise to Config S (~590 KB at int4), so measure it separately
and report it as its own line rather than folding it into the headline.

**4. `seq_len` is 512 and the real product needs ~64.**
A 15-word utterance is ~20 tokens. Cutting `seq_len` shrinks the KV cache 8×
(676 KB → ~85 KB) and reduces attention work. Requires re-exporting the model
or clamping at runtime. Mostly a memory win; measure whether it buys time.

**5. Both cores.** `matmul` is trivially parallel over output rows (the
`#pragma omp parallel for` upstream says so). Splitting across cores could
approach 2×. **Caveat:** on the real device core 0 runs WiFi and TLS and core 1
runs the face, so a two-core kernel may not be available in production. Measure
it, but report it separately from single-core numbers.

**6. Compiler flags.** `-O2 -ffast-math` are already set for this component.
Check whether `-funroll-loops` or targeting the S3 explicitly helps. Low payoff;
do it last and only if the profile says the loop is not already SIMD.

---

## 6. How to measure without fooling yourself

The harness already reports tok/s, p50, p99 and memory. Keep that output format
stable so results are comparable across attempts.

- **Re-run the fp32 PSRAM baseline yourself** before your first change. If your
  number differs meaningfully from 31.4 tok/s, stop and work out why — the
  discrepancy is more interesting than the optimisation.
- **Report p99, not just the mean.** Something that shares a chip with an
  animated face cares about worst-case, not average.
- **Verify output is still correct after every change.** The benchmark prints a
  sample. It should stay coherent English prose. Quantisation will change the
  exact words — that is fine — but garbage means a broken kernel, and a fast
  wrong kernel is worth nothing. Say explicitly in your report whether output
  was still coherent for each variant.
- **One change at a time**, measured. A combined 3× that cannot be attributed
  is much less useful than three attributed 1.4×s.

---

## 7. Environment traps that have already cost time here

- **Python `python` vs `python3` mismatch.** If a build complains the
  environment is "not consistent with the project configuration", the two
  symlinks got crossed. Fix: `idf.py fullclean` once, then rebuild.
- **The board can boot into download mode and stay there.** If the log shows
  `boot:0x0 (DOWNLOAD)` and "waiting for download", the BOOT/GPIO0 button is
  held or jumpered. Release it and tap RESET. No software reset overrides a
  strapping pin.
- **Two USB sockets, different behaviour.** The native-USB socket enumerates as
  a "USB JTAG_serial debug unit" and is fast and reliable for bulk serial. The
  other sits behind a CH343 UART bridge, is better for auto-reset, and **drops
  bytes on large transfers**. The console is currently configured for
  USB-Serial-JTAG; if you move the cable you must change
  `CONFIG_ESP_CONSOLE_*` to match or you will get silence.
- **Build output must never be committed.** The repo has a root `.gitignore`
  covering `build/`, `sdkconfig` and `*.bin`, because a spike commit once
  landed 1,556 build artifacts. Check `git status` before committing.

---

## 8. Scope guards — what NOT to do

- **Do not rewrite the model architecture.** No new attention variants, no
  architecture search. This is a kernel task.
- **Do not train anything.** No corpus, no fine-tuning. Out of scope.
- **Do not touch `firmware/`.** This spike is standalone by design so the
  working robot is never at risk.
- **Do not vendor large binaries.** Weights are fetched, never committed.
- **Do not merge to `main`.**
- **Do not chase the last 10%** with hand-written assembly before the obvious
  wins (ESP-DSP, int8, memory placement) are measured. If you find yourself
  writing intrinsics in week one, something has been skipped.

---

## 9. What to deliver

1. **Code on `spike/tinylm-s3`**, committed incrementally — one commit per
   optimisation, each message stating the measured before/after.
2. **A results table appended to `spikes/tinylm-s3/README.md`**: variant,
   tok/s, ms/token, p50/p99, memory, and whether output stayed coherent.
3. **The profile breakdown** from §4, since it is reusable knowledge regardless
   of how the optimisation goes.
4. **A recommendation**, in plain terms: extrapolate your best result to Config
   S (1,179,648 non-embedding parameters — scale from stories260K's 227,840)
   and say whether a ~15-word line lands under a second. Then say whether you
   think the local-model direction is worth continuing. **A well-argued "no" is
   a valid and valuable deliverable** — this whole line of work exists to be
   killed cheaply if it does not pan out.

Write the recommendation for a volunteer hacker group deciding where to spend
its weekends, not for a compiler engineer. Numbers first, then what they mean.
