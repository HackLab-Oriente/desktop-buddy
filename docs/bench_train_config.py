#!/usr/bin/env python3
"""Find the fastest (device, batch, accumulation) for a given model size.

Run from inside a llama2.c clone, with its venv active:

    python bench_train_config.py                 # XS ladder size
    python bench_train_config.py --dim=64 --n_layers=5

Every combination does the SAME work per step (batch x accumulation = 128
sequences), so the winner is simply the lowest ms/step. Machines differ a
lot here — on one Apple laptop four narrow passes beat one wide pass,
which is the opposite of the usual GPU advice — so measure, don't assume.

No data needed: it times forward+backward on random tokens, which is what
a training step actually costs.
"""
import argparse, time, itertools
import torch
from model import ModelArgs, Transformer

p = argparse.ArgumentParser()
p.add_argument("--dim", type=int, default=48)
p.add_argument("--n_layers", type=int, default=4)
p.add_argument("--n_heads", type=int, default=4)
p.add_argument("--n_kv_heads", type=int, default=4)
p.add_argument("--vocab_size", type=int, default=512)
p.add_argument("--max_seq_len", type=int, default=256)
p.add_argument("--effective_batch", type=int, default=128)
p.add_argument("--steps", type=int, default=12, help="timed steps per config")
a = p.parse_args()

devices = ["cpu"] + (["mps"] if torch.backends.mps.is_available() else [])
if torch.cuda.is_available():
    devices = ["cuda"]                      # no point timing cpu against a real GPU
# (micro_batch, accumulation) pairs that all sum to effective_batch
splits = [(a.effective_batch // n, n) for n in (1, 2, 4, 8)
          if a.effective_batch % n == 0 and a.effective_batch // n >= 1]

args = ModelArgs(dim=a.dim, n_layers=a.n_layers, n_heads=a.n_heads,
                 n_kv_heads=a.n_kv_heads, vocab_size=a.vocab_size,
                 max_seq_len=a.max_seq_len, dropout=0.0)
nparams = sum(p_.numel() for p_ in Transformer(args).parameters())
print(f"model: dim={a.dim} layers={a.n_layers} heads={a.n_heads} "
      f"kv={a.n_kv_heads} vocab={a.vocab_size} seq={a.max_seq_len}")
print(f"       {nparams:,} parameters · effective batch {a.effective_batch}\n")
print(f"{'device':>7} {'micro-batch':>12} {'accum':>6} {'ms/step':>9}   {'vs best':>8}")

results = []
for dev, (mb, accum) in itertools.product(devices, splits):
    try:
        torch.manual_seed(0)
        model = Transformer(args).to(dev)
        opt = torch.optim.AdamW(model.parameters(), lr=1e-4)
        x = torch.randint(0, a.vocab_size, (mb, a.max_seq_len), device=dev)
        y = torch.randint(0, a.vocab_size, (mb, a.max_seq_len), device=dev)

        def one_step():
            for _ in range(accum):
                model(x, y)
                model.last_loss.backward()
            opt.step(); opt.zero_grad(set_to_none=True)

        for _ in range(3):                  # warm up kernels
            one_step()
        if dev == "mps": torch.mps.synchronize()
        elif dev == "cuda": torch.cuda.synchronize()

        t0 = time.perf_counter()
        for _ in range(a.steps):
            one_step()
        if dev == "mps": torch.mps.synchronize()
        elif dev == "cuda": torch.cuda.synchronize()
        ms = (time.perf_counter() - t0) / a.steps * 1000
        results.append((ms, dev, mb, accum))
        print(f"{dev:>7} {mb:>12} {accum:>6} {ms:>8.1f}")
        del model, opt, x, y
    except RuntimeError as e:                # usually out of memory
        print(f"{dev:>7} {mb:>12} {accum:>6}      failed  ({str(e)[:40]})")

if results:
    results.sort()
    best = results[0]
    print()
    for ms, dev, mb, accum in results:
        print(f"{dev:>7} {mb:>12} {accum:>6} {ms:>8.1f}   {ms/best[0]:>7.2f}x")
    print(f"\nfastest: --device={best[1]} --batch_size={best[2]} "
          f"--gradient_accumulation_steps={best[3]}   ({best[0]:.0f} ms/step)")
    tot = best[0] * 20000 / 1000 / 60
    print(f"a full 20,000-step run at that setting: ~{tot:.0f} min")
