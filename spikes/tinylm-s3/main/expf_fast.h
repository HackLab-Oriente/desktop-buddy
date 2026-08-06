// Fast expf for softmax and SiLU. newlib's expf costs ~420 cycles on the S3
// and this workload calls it thousands of times per token (attention softmax
// over every past position x every head x every layer, the sampler's softmax
// over all 512 logits, and SwiGLU's sigmoid); together that was ~4 ms of the
// ~12 ms token. This one is ~15 cycles.
//
// exp(x) = 2^(x/ln2), split into integer exponent + fraction: the integer
// part is built directly in the float's exponent field, the fractional 2^f
// comes from a cubic minimax on [0,1). |relative error| < 2.5e-4 — an order
// of magnitude below the int8 quantisation noise already accepted, and
// softmax renormalises anyway. Clamps: 0 below -87 (exp would underflow),
// capped above +88 (would overflow to inf).
#pragma once
#include <stdint.h>

static inline float expf_fast(float x) {
    if (x < -87.0f) return 0.0f;
    if (x > 88.0f) x = 88.0f;
    const float t = x * 1.4426950408889634f;  // x / ln 2
    // floor(t) via truncation shifted positive (valid because t > -127, and
    // trunc == floor for positive values)
    const int32_t e = (int32_t)(t + 192.0f) - 192;
    const float f = t - (float)e;  // [0, 1)
    const float p = 0.99992522f +
                    f * (0.69583354f + f * (0.22606716f + f * 0.07811395f));
    const union { uint32_t u; float v; } b = {(uint32_t)(e + 127) << 23};
    return b.v * p;
}
