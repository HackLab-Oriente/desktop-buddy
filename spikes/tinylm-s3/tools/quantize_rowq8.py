#!/usr/bin/env python3
"""fp32 llama2.c checkpoint -> row-quantised int8 ("BQR1") for the S3 spike.

Why not upstream runq.c's Q8_0 export: its groups run over the *flattened*
tensor with one global group size, and stories260K's hidden_dim=172 forces
that group size down to 4 (172 = 4x43), which defeats the S3's 16-lane int8
SIMD and doubles the scale overhead. Here we quantise PER ROW instead:

  - one fp32 scale per output row (the dot-product unit of matmul), and
  - each row's int8 data padded with zeros to a multiple of 16 bytes,

which is exactly the contract of dsps_dp_s8_aes3 (ee.vld.128 needs 16-byte
alignment; len must be a multiple of 16; zero padding adds 0 to the dot).

Layout (all little-endian, every section a multiple of 16 bytes):
  header (64 B): u32 magic 'BQR1', 7x i32 config, u32 shared_classifier, pad
  fp32:  rms_att (L*dim), rms_ffn (L*dim), rms_final (dim)
  per tensor, in order tokens,wq,wk,wv,wo,w1,w2,w3 (layers concatenated):
         int8 rows (row length padded to mult. of 16), then fp32 row scales
  wcls is shared with tokens (stories260K has a shared classifier).

Pure stdlib on purpose: no numpy/torch on the build machine.
"""
import struct, sys
from array import array

MAGIC = 0x31525142  # 'BQR1' little-endian


def pad16(n):
    return (n + 15) // 16 * 16


def quantize_rows(vals, rows, cols):
    """Per-row int8: returns (bytes of padded rows, array of row scales)."""
    npad = pad16(cols)
    q = bytearray(rows * npad)
    scales = array("f", [0.0] * rows)
    worst = 0.0
    for r in range(rows):
        row = vals[r * cols:(r + 1) * cols]
        amax = max(abs(v) for v in row)
        scale = amax / 127.0 if amax > 0 else 1.0
        scales[r] = scale
        base = r * npad
        for c, v in enumerate(row):
            iv = int(round(v / scale))
            iv = max(-127, min(127, iv))
            q[base + c] = iv & 0xFF
            worst = max(worst, abs(v - iv * scale))
    return bytes(q), scales, worst


def main(src, dst):
    blob = open(src, "rb").read()
    cfg = struct.unpack_from("<7i", blob, 0)
    dim, hidden, L, heads, kv_heads, vocab, seq = cfg
    kv_dim = dim * kv_heads // heads
    shared = 1 if vocab > 0 else 0
    f = array("f")
    f.frombytes(blob[28:])
    if sys.byteorder != "little":
        f.byteswap()

    off = 0

    def take(n):
        nonlocal off
        part = f[off:off + n]
        off += n
        return part

    tokens = take(vocab * dim)
    rms_att = take(L * dim)
    wq = take(L * dim * dim)
    wk = take(L * dim * kv_dim)
    wv = take(L * dim * kv_dim)
    wo = take(L * dim * dim)
    rms_ffn = take(L * dim)
    w1 = take(L * dim * hidden)
    w2 = take(L * hidden * dim)
    w3 = take(L * dim * hidden)
    rms_final = take(dim)
    off += seq * (dim // heads)  # skip legacy freq_cis
    assert off == len(f), (off, len(f))
    assert shared == 1, "stories260K should have a shared classifier"

    out = bytearray()
    out += struct.pack("<I7iI", MAGIC, *cfg, shared)
    out += b"\0" * (64 - len(out))
    for a in (rms_att, rms_ffn, rms_final):
        out += a.tobytes()
    assert len(out) % 16 == 0

    # (name, values, rows=outputs, cols=inputs). Rows are matmul outputs, so
    # a row is one dot product and one scale.
    tensors = [
        ("tokens", tokens, vocab, dim),
        ("wq", wq, L * dim, dim),
        ("wk", wk, L * kv_dim, dim),
        ("wv", wv, L * kv_dim, dim),
        ("wo", wo, L * dim, dim),
        ("w1", w1, L * hidden, dim),
        ("w2", w2, L * dim, hidden),
        ("w3", w3, L * hidden, dim),
    ]
    for name, vals, rows, cols in tensors:
        q, scales, worst = quantize_rows(vals, rows, cols)
        out += q
        out += scales.tobytes()
        assert len(out) % 16 == 0, name
        print(f"  {name:6s} {rows:4d} rows x {cols:3d} (pad {pad16(cols):3d})"
              f"  max err {worst:.6f}")

    open(dst, "wb").write(out)
    print(f"wrote {dst}: {len(out)} bytes "
          f"(fp32 was {len(blob)}; {len(blob)/len(out):.1f}x smaller)")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
