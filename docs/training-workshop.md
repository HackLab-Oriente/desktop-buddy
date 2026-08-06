# Training workshop: teach the buddy to write its own lines

A hands-on recipe for training a tiny language model — from zero ML
experience to a model of your own running on the buddy. This is the
pre-workshop session voted for in [workshops.md](workshops.md); it builds on
the kernel results in `spikes/tinylm-s3` (branch `spike/tinylm-s3`), where
the inference side is already solved: **152 tok/s** for a 260K-param model
on the ESP32-S3, output streaming to the face.

Nobody attending needs prior AI/ML experience. If you can run terminal
commands, you can train a model.

---

## 1. Training, in five sentences

A language model is a large pile of numbers ("parameters" or "weights") that
turns *the words so far* into *a guess about the next word-piece*. Training
shows it millions of text snippets; after every guess, an algorithm nudges
each number so that guess would have been slightly less wrong. Repeat a few
million times and the guesses get good — that's the entire trick. Your job
as the trainer is only to pick the model's **size and shape** (the flags
below), point it at **data**, and **watch a number fall** until it stops
falling. Quality control is reading what it writes.

Two numbers matter while it runs:

- **train loss** — how wrong its guesses are on text it is learning from.
- **val loss** — how wrong on text it has *never seen*. This is the honest
  one. When val loss stops falling, more training buys nothing.

## 2. What you need

- A Mac (Apple-Silicon GPU is used automatically via PyTorch's `mps`
  device) or free Google Colab. No paid cloud, no account keys.
- Python 3.10+, ~3 GB of disk, one evening.
- The training code is the same repo our kernel came from —
  [karpathy/llama2.c](https://github.com/karpathy/llama2.c). It writes the
  exact `.bin` format the buddy already executes. Train → quantise → flash.

## 3. First model: known data before our data

Train on **TinyStories** (a public dataset of simple children's stories)
before touching a buddy corpus. This separates "learning the pipeline" from
"building our dataset" — and gives you a direct on-device comparison against
the reference `stories260K` model.

```bash
git clone https://github.com/karpathy/llama2.c.git && cd llama2.c
pip install -r requirements.txt
python tinystories.py download                    # ~1.5 GB, once
python tinystories.py train_vocab --vocab_size=512
python tinystories.py pretokenize --vocab_size=512
```

(`train_vocab` builds the 512-piece vocabulary; `pretokenize` pre-chews the
dataset into token ids. Both are one-time and take a few minutes.)

Then the training run — this shape is ~150K parameters:

```bash
python train.py \
  --vocab_source=custom --vocab_size=512 \
  --dim=48 --n_layers=4 --n_heads=4 --n_kv_heads=4 \
  --max_seq_len=256 --batch_size=32 \
  --device=mps --dtype=float32 --compile=False \
  --eval_interval=200 --always_save_checkpoint=True
```

It prints a loss line every few seconds. Let it run until val loss flattens
(ballpark: 30–90 min on an M-series for this size; Ctrl-C is safe, it
checkpoints to `out/`). Read its stories with:

```bash
python sample.py --checkpoint=out/ckpt.pt
```

The two Mac-specific flags are `--device=mps --compile=False` —
`torch.compile` and Apple GPUs don't get along.

## 4. The knobs, in plain language

Everything you pass to `train.py` falls into two families: flags that shape
**the model itself** (they change what ends up on the buddy) and flags that
shape **the training process** (they change how long it takes to get there,
but not what ships).

### Flags that shape the model

| flag | plain meaning | turning it UP means |
|---|---|---|
| `--dim` | width: the size of the "thought" the model carries per word-piece | more nuance per word, better word choice — and cost grows roughly with dim², so this is the most expensive knob |
| `--n_layers` | depth: how many times the thought gets refined before predicting | better grammar and sentence-level coherence; cost grows linearly |
| `--n_heads` | how many things attention can "look for" at once in the earlier words (one head might track the subject, another the mood) | diminishing returns at our scale; keep dim/n_heads (the per-head size) between 8 and 16 |
| `--n_kv_heads` | a memory-saving variant: heads can share their lookup tables | fewer = smaller KV cache on the buddy; `stories260K` uses half as many kv-heads as heads and it costs nothing noticeable |
| `--vocab_size` | how many word-pieces exist. 512 means common words are 1 piece, rare ones are spelled from fragments | bigger vocab = fewer pieces per sentence (faster reading aloud) but a bigger embedding table; **keep 512 for everything in this workshop** so all models share tooling |
| `--max_seq_len` | how far back it can see, in pieces | we generate 15-word one-liners; 256 is already generous. On the buddy this sets the KV-cache RAM (it's why the firmware caps it) |
| `--dropout` | randomly ignore a fraction of the network each step, to discourage memorising | worth raising (0.1–0.2) in Part 2, when our corpus is small and memorisation is the main risk |

**How the knobs become a parameter count** (≈ speed on the buddy): each
layer costs about `4·dim²` (attention) `+ 3·dim·hidden` (feed-forward,
where hidden ≈ 2.7·dim), and the embedding table costs `vocab × dim` on
top. You never need to compute this — the first line `train.py` prints is
the exact count. Smaller count = faster buddy, dumber model. That trade is
the whole game.

### Flags that shape only the training run

| flag | plain meaning | what to know |
|---|---|---|
| `--batch_size` | snippets digested per nudge | bigger = better GPU use, more RAM; it barely changes final quality. Lower it if you run out of memory |
| `--learning_rate` | nudge size | too high: loss explodes or jitters. Too low: glacial. The default is tuned for this repo — leave it until you have a reason |
| `--max_iters` | total nudges before stopping | with `--always_save_checkpoint` you can just Ctrl-C when val loss flattens instead |
| `--eval_interval` | how often val loss is measured | 200 keeps the feedback loop tight |
| `--device` / `--dtype` / `--compile` | which chip does the math and how | mechanics only — zero effect on what the model learns. Mac: `mps/float32/False`. Colab GPU: `cuda/bfloat16/True` |

### What failure looks like (so nobody panics)

- **Loss goes UP or to `nan`** → learning rate too high, or a bad flag
  combo. Kill it, restart; nothing is damaged.
- **Samples are word salad** → undertrained (val loss still falling — keep
  going) or the model is too small for the data's variety.
- **Samples repeat one phrase forever** → classic tiny-model failure;
  more data variety, a bit more size, or more training usually fixes it.
- **val loss rises while train loss keeps falling** → memorisation
  (overfitting). For TinyStories that's a stop sign. For our tiny buddy
  corpus in Part 2 it is *expected* — see §7.

## 5. The size ladder

The session's core experiment: same data, four sizes, then the group reads
the output and picks the smallest one that sounds good. Buddy speeds are
derived from the **measured** int8-PSRAM result in the spike (88 tok/s at
228K non-embedding params; speed scales inversely with that count — this is
the realistic in-firmware number, not the internal-RAM benchmark headline):

| ladder | flags | ≈ params | buddy speed (int8, PSRAM) | 15-word line |
|---|---|---|---|---|
| XS | `--dim=48 --n_layers=4 --n_heads=4 --n_kv_heads=4` | ~140K | ~180 tok/s | instant |
| S *(= stories260K's shape)* | `--dim=64 --n_layers=5 --n_heads=8 --n_kv_heads=4` | ~260K | **88 tok/s (measured)** | ~0.3 s |
| M | `--dim=64 --n_layers=6 --n_heads=8 --n_kv_heads=4` | ~305K | ~74 tok/s | ~0.35 s |
| L | `--dim=80 --n_layers=6 --n_heads=8 --n_kv_heads=8` | ~520K | ~42 tok/s | ~0.55 s |
| XL *(≈ "Config S")* | `--dim=128 --n_layers=6 --n_heads=8 --n_kv_heads=4` | ~1.3M | ~17 tok/s | ~1.3 s |

Tokens stream to the face as they're generated, so perceived latency is the
first token, not the full line — even XL feels responsive.

## 6. Putting a model on the buddy

`train.py` writes `out/model.bin` (fp32, same format as stories260K).
From the `spikes/tinylm-s3` directory on branch `spike/tinylm-s3`:

```bash
esptool.py --chip esp32s3 -p PORT write-flash 0x710000 model.bin
python3 tools/quantize_rowq8.py model.bin model-rowq8.bin
esptool.py --chip esp32s3 -p PORT write-flash 0x910000 model-rowq8.bin
idf.py -p PORT flash monitor
```

The benchmark reads the model's shape from the file headers, so it adapts to
any ladder size unmodified. Two gotchas:

- **The fp32 file must stay under 2 MB** or it overlaps the int8 blob at
  partition offset `+0x200000`. XS/S/M fit; **L (~2.1 MB) and XL (~5 MB) do
  not — for those, skip the fp32 flash entirely** and flash only the int8
  blob (the two fp32 benchmark passes will print garbage; ignore them, the
  int8 passes are self-contained).
- **A new tokenizer must be re-embedded.** Each `train_vocab` run produces
  a different `tok512.bin`, and the firmware carries it inside `tok512.h`.
  If you skip this, a perfectly healthy model decodes to gibberish.
  Regenerate with:

```bash
python3 -c "
data = open('data/tok512.bin','rb').read()
with open('tok512.h','w') as f:
    f.write('// llama2.c tokenizer, embedded. %d bytes.\n' % len(data))
    f.write('#pragma once\n#include <stdint.h>\n')
    f.write('static const uint32_t kTokBytes = %d;\n' % len(data))
    f.write('static const uint8_t kTok[%d] = {\n' % len(data))
    for i in range(0, len(data), 16):
        f.write('    ' + ''.join('0x%02X, ' % b for b in data[i:i+16]) + '\n')
    f.write('};\n')
"
```

then replace `spikes/tinylm-s3/main/tok512.h` with it and rebuild.

## 7. Part 2: the buddy corpus

Once the group's **line bank** exists (the mood-labelled utterance file —
which ships in a pack in its own right), it doubles as training data. The
format is one utterance per line, mood as a plain-text prefix:

```
happy: Ooh, a friend! Today is officially the best day.
sleepy: Five more minutes. Maybe ten. Wake me for snacks.
angry: I have logged this offense. There will be consequences.
```

At generation time the firmware feeds `happy: ` as the prompt and the model
completes it in that register. Plain-text prefixes (not special `<tokens>`)
keep the tokenizer pipeline stock — the BPE learns them as ordinary frequent
pieces.

Session prep still to build (small, ~30 lines each, following
`tinystories.py`'s pattern): a `pretokenize` script for our corpus file, and
a sampling script that prompts with each mood. **Two expectations to set:**

- A 150K model on a few thousand lines will **memorise heavily** — it
  behaves like a fuzzy, recombining line bank. That is not failure; it is
  precisely the A/B the group should judge *by ear* against the plain line
  bank (val loss stops being meaningful at this data size — read the
  output instead).
- **The mood prefixes are baked in at training time**, so the group's final
  emotion set needs to be settled first. This is the same governance
  dependency as moving `kEmotions` out of C++ into pack data: the moods are
  shared vocabulary between the face, the packs, and now the model.

## 8. Where this fits

- Kernel/speed work: done — `spikes/tinylm-s3/README.md` (Step 0 and 0.5).
- This session: can a small model *sound right*? Decided by listening.
- The decision it feeds: local model vs line bank vs hybrid — all three sit
  behind the same `brain.ask` bus event, so whichever the group picks is a
  pack/config choice, not an architecture change.
