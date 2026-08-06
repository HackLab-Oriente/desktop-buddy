// int8 row-quantised inference for the S3 — avenues 2 and 3 of the brief.
//
// Weights come from the "BQR1" blob written by tools/quantize_rowq8.py and
// flashed at model-partition offset 0x200000 (the fp32 checkpoint keeps the
// first megabyte). One fp32 scale per output row; each row's int8 data is
// zero-padded to a multiple of 16 bytes. That is exactly the contract of
// dsps_dp_s8_aes3: ee.vld.128 loads want 16-byte-aligned operands and a
// length that is a multiple of 16, and the padding zeros add 0 to the dot.
//
// The same loader serves avenue 2 (weights in PSRAM) and avenue 3 (weights in
// internal RAM): the caller picks the heap caps. Everything else — run state,
// fp32 KV cache in PSRAM, attention/RoPE/rmsnorm code — is identical between
// the two, so the only variable measured is where the weights live.
//
// CAUTION, learned from the assembly: dsps_dp_s8_aes3 pipelines its loads and
// reads 16 bytes PAST the end of both operands on the final iteration. Every
// buffer it touches is allocated with 16 bytes of slack.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsps_dotprod.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "expf_fast.h"
#include "prof.h"
#include "run_api.h"

#define BQR1_MAGIC 0x31525142u
#define BQR1_FLASH_OFFSET 0x200000u

static const char* TAG = "tinylm-q8";

static inline int pad16(int n) { return (n + 15) / 16 * 16; }

typedef struct {
    const int8_t* q;  // rows*npad int8, each row 16B-aligned
    const float* s;   // one scale per row
    int rows, cols, npad;
} QTensor;

struct QTransformer {
    Config c;
    int kv_dim, head_size;
    float rope_freq[16];  // 10000^(-head_dim/head_size), one per rotation pair
    float *rms_att, *rms_ffn, *rms_final;              // fp32, in the blob
    QTensor tokens, wq, wk, wv, wo, w1, w2, w3;        // layers concatenated
    uint8_t* blob;                                     // owned aligned copy
    // run state (PSRAM in both variants; only the weights move)
    float *x, *xb, *xb2, *hb, *hb2, *q, *att, *logits;
    float *key_cache, *value_cache;
    int8_t *xq, *hq;                                   // quantised activations
};

static void* qcalloc_psram(size_t n, size_t sz) {
    void* p = heap_caps_aligned_calloc(16, n, sz, MALLOC_CAP_SPIRAM);
    return p ? p : heap_caps_aligned_calloc(16, n, sz, MALLOC_CAP_DEFAULT);
}

// Walk the blob laying out one tensor; every section is a multiple of 16
// bytes by construction, so pointers into a 16B-aligned copy stay aligned.
static const uint8_t* qtensor_init(QTensor* t, const uint8_t* p, int rows, int cols) {
    t->rows = rows; t->cols = cols; t->npad = pad16(cols);
    t->q = (const int8_t*)p;             p += (size_t)rows * t->npad;
    t->s = (const float*)p;              p += (size_t)rows * sizeof(float);
    return p;
}

QTransformer* build_transformer_q8(uint32_t weight_caps) {
    const esp_partition_t* part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "model");
    if (!part) { ESP_LOGE(TAG, "no 'model' partition"); return NULL; }
    const void* base = NULL;
    static esp_partition_mmap_handle_t handle;
    if (esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA,
                           &base, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "esp_partition_mmap failed"); return NULL;
    }
    const uint8_t* src = (const uint8_t*)base + BQR1_FLASH_OFFSET;
    uint32_t magic; memcpy(&magic, src, 4);
    if (magic != BQR1_MAGIC) {
        ESP_LOGE(TAG, "no BQR1 blob at +0x%x (flash tools/quantize_rowq8.py output there)",
                 BQR1_FLASH_OFFSET);
        return NULL;
    }

    QTransformer* t = calloc(1, sizeof *t);
    memcpy(&t->c, src + 4, sizeof(Config));
    const Config* c = &t->c;
    t->kv_dim = c->dim * c->n_kv_heads / c->n_heads;
    t->head_size = c->dim / c->n_heads;
    if (t->head_size / 2 > 16) { ESP_LOGE(TAG, "head_size too large"); free(t); return NULL; }
    // RoPE frequencies depend only on the pair index — hoist the powf here,
    // and the per-position sin/cos to once per token in forward_q8 (they were
    // being recomputed per LAYER: 160 transcendental calls/token, ~0.9 ms).
    for (int j = 0; j < t->head_size / 2; j++)
        t->rope_freq[j] = powf(10000.0f, -(2.0f * j) / t->head_size);

    // total blob size, mirroring the converter
    const int L = c->n_layers, dim = c->dim, hid = c->hidden_dim;
    size_t sz = 64 + (size_t)(2 * L * dim + dim) * 4;
    struct { int rows, cols; } shapes[] = {
        {c->vocab_size, dim}, {L * dim, dim}, {L * t->kv_dim, dim},
        {L * t->kv_dim, dim}, {L * dim, dim}, {L * hid, dim},
        {L * dim, hid}, {L * hid, dim},
    };
    for (unsigned i = 0; i < sizeof shapes / sizeof *shapes; i++)
        sz += (size_t)shapes[i].rows * (pad16(shapes[i].cols) + 4);

    // 16B slack: dsps_dp_s8_aes3 over-reads 16 bytes past the last row.
    t->blob = heap_caps_aligned_alloc(16, sz + 16, weight_caps);
    if (!t->blob) {
        ESP_LOGE(TAG, "no room for %u B of weights in caps 0x%x", (unsigned)sz,
                 (unsigned)weight_caps);
        free(t); return NULL;
    }
    memcpy(t->blob, src, sz);

    const uint8_t* p = t->blob + 64;
    t->rms_att = (float*)p;    p += (size_t)L * dim * 4;
    t->rms_ffn = (float*)p;    p += (size_t)L * dim * 4;
    t->rms_final = (float*)p;  p += (size_t)dim * 4;
    QTensor* tensors[] = {&t->tokens, &t->wq, &t->wk, &t->wv,
                          &t->wo, &t->w1, &t->w2, &t->w3};
    for (unsigned i = 0; i < sizeof tensors / sizeof *tensors; i++)
        p = qtensor_init(tensors[i], p, shapes[i].rows, shapes[i].cols);
    if ((size_t)(p - t->blob) != sz) {
        ESP_LOGE(TAG, "blob layout mismatch: %u != %u", (unsigned)(p - t->blob),
                 (unsigned)sz);
        heap_caps_free(t->blob); free(t); return NULL;
    }

    // run state — PSRAM regardless of where the weights went (see header)
    t->x = qcalloc_psram(dim, 4);       t->xb = qcalloc_psram(dim, 4);
    t->xb2 = qcalloc_psram(dim, 4);     t->hb = qcalloc_psram(hid, 4);
    t->hb2 = qcalloc_psram(hid, 4);     t->q = qcalloc_psram(dim, 4);
    t->att = qcalloc_psram((size_t)c->n_heads * c->seq_len, 4);
    t->logits = qcalloc_psram(c->vocab_size, 4);
    t->key_cache = qcalloc_psram((size_t)L * c->seq_len * t->kv_dim, 4);
    t->value_cache = qcalloc_psram((size_t)L * c->seq_len * t->kv_dim, 4);
    // +16 slack again: these are dot-product operands too
    t->xq = qcalloc_psram(pad16(dim) + 16, 1);
    t->hq = qcalloc_psram(pad16(hid) + 16, 1);
    if (!t->x || !t->xb || !t->xb2 || !t->hb || !t->hb2 || !t->q || !t->att ||
        !t->logits || !t->key_cache || !t->value_cache || !t->xq || !t->hq) {
        ESP_LOGE(TAG, "q8 run-state alloc failed"); return NULL;
    }
    ESP_LOGI(TAG, "q8 weights: %u B in %s", (unsigned)sz,
             (weight_caps & MALLOC_CAP_INTERNAL) ? "INTERNAL RAM" : "PSRAM");
    return t;
}

void free_transformer_q8(QTransformer* t) {
    if (!t) return;
    heap_caps_free(t->blob);
    float* fbufs[] = {t->x, t->xb, t->xb2, t->hb, t->hb2, t->q,
                      t->att, t->logits, t->key_cache, t->value_cache};
    for (unsigned i = 0; i < sizeof fbufs / sizeof *fbufs; i++) free(fbufs[i]);
    free(t->xq); free(t->hq);
    free(t);
}

// x -> int8 with one scale for the whole vector; zero the pad so it adds
// nothing to the dot product. Returns the scale.
static float quantize_vec(int8_t* out, const float* in, int n, int npad) {
    float amax = 0.0f;
    for (int j = 0; j < n; j++) { float a = fabsf(in[j]); if (a > amax) amax = a; }
    const float scale = amax > 0.0f ? amax / 127.0f : 1.0f;
    const float inv = 1.0f / scale;
    // Round-to-nearest via the 1.5*2^23 magic constant: the add forces the
    // value into a fixed-exponent float whose low mantissa bits ARE the
    // integer (two's complement included) — a few cycles vs a lrintf call.
    // |in*inv| <= 127 by construction of the scale, so no clamp is needed
    // and we are far inside the trick's +/-2^22 validity range.
    for (int j = 0; j < n; j++) {
        union { float v; int32_t i; } u;
        u.v = in[j] * inv + 12582912.0f;
        out[j] = (int8_t)(u.i - 0x4B400000);
    }
    memset(out + n, 0, npad - n);
    return scale;
}

// One quantised matmul: d rows of W (starting at row_off) dot the quantised
// activation. W (d,cols) @ x (cols,) -> xout (d,)
static void matmul_q8(float* xout, const int8_t* xq, float xs,
                      const QTensor* w, int row_off, int d) {
    const int npad = w->npad;
    const int8_t* row = w->q + (size_t)row_off * npad;
    const float* s = w->s + row_off;
    for (int i = 0; i < d; i++) {
        int32_t ival = 0;
        dsps_dp_s8_aes3(row, xq, &ival, npad);
        xout[i] = (float)ival * s[i] * xs;
        row += npad;
    }
}

float* forward_q8(QTransformer* t, int token, int pos) {
    const Config* c = &t->c;
    const int dim = c->dim, hid = c->hidden_dim, kv_dim = t->kv_dim;
    const int head_size = t->head_size, kv_mul = c->n_heads / c->n_kv_heads;
    const int npad_d = pad16(dim), npad_h = pad16(hid);
    float* x = t->x;

    // token embedding: dequantise one row of the shared tokens table
    PROF_START();
    {
        const int8_t* row = t->tokens.q + (size_t)token * t->tokens.npad;
        const float s = t->tokens.s[token];
        for (int j = 0; j < dim; j++) x[j] = row[j] * s;
    }
    PROF_MARK(PROF_OTHER);

    // RoPE angles depend only on (pos, pair index) — same for every layer.
    float fcr[16], fci[16];
    for (int j = 0; j < head_size / 2; j++) {
        const float val = pos * t->rope_freq[j];
        fcr[j] = cosf(val);
        fci[j] = sinf(val);
    }
    PROF_MARK(PROF_ROPE);

    for (int l = 0; l < c->n_layers; l++) {
        rmsnorm(t->xb, x, t->rms_att + l * dim, dim);
        PROF_MARK(PROF_RMSNORM);

        const int loff = l * c->seq_len * kv_dim;
        float* k = t->key_cache + loff + pos * kv_dim;
        float* v = t->value_cache + loff + pos * kv_dim;

        const float xs = quantize_vec(t->xq, t->xb, dim, npad_d);
        PROF_MARK(PROF_QUANT);
        matmul_q8(t->q, t->xq, xs, &t->wq, l * dim, dim);
        matmul_q8(k, t->xq, xs, &t->wk, l * kv_dim, kv_dim);
        matmul_q8(v, t->xq, xs, &t->wv, l * kv_dim, kv_dim);
        PROF_MARK(PROF_MM_QKV);

        // RoPE — same maths as the fp32 path, with the angles precomputed
        for (int i = 0; i < dim; i += 2) {
            const int j = (i % head_size) >> 1;
            const int rotn = i < kv_dim ? 2 : 1;
            for (int vv = 0; vv < rotn; vv++) {
                float* vec = vv == 0 ? t->q : k;
                float v0 = vec[i], v1 = vec[i + 1];
                vec[i] = v0 * fcr[j] - v1 * fci[j];
                vec[i + 1] = v0 * fci[j] + v1 * fcr[j];
            }
        }
        PROF_MARK(PROF_ROPE);

        // multihead attention — identical to the fp32 path (fp32 KV cache)
        for (int h = 0; h < c->n_heads; h++) {
            float* q = t->q + h * head_size;
            float* att = t->att + h * c->seq_len;
            for (int tt = 0; tt <= pos; tt++) {
                float* kk = t->key_cache + loff + tt * kv_dim + (h / kv_mul) * head_size;
                float score = 0.0f;
                for (int i = 0; i < head_size; i++) score += q[i] * kk[i];
                att[tt] = score / sqrtf(head_size);
            }
            softmax(att, pos + 1);
            float* xb = t->xb + h * head_size;
            memset(xb, 0, head_size * sizeof(float));
            for (int tt = 0; tt <= pos; tt++) {
                float* vv = t->value_cache + loff + tt * kv_dim + (h / kv_mul) * head_size;
                float a = att[tt];
                for (int i = 0; i < head_size; i++) xb[i] += a * vv[i];
            }
        }
        PROF_MARK(PROF_ATTN);

        const float os = quantize_vec(t->xq, t->xb, dim, npad_d);
        PROF_MARK(PROF_QUANT);
        matmul_q8(t->xb2, t->xq, os, &t->wo, l * dim, dim);
        PROF_MARK(PROF_MM_WO);

        for (int i = 0; i < dim; i++) x[i] += t->xb2[i];
        PROF_MARK(PROF_OTHER);

        rmsnorm(t->xb, x, t->rms_ffn + l * dim, dim);
        PROF_MARK(PROF_RMSNORM);

        const float fs = quantize_vec(t->xq, t->xb, dim, npad_d);
        PROF_MARK(PROF_QUANT);
        matmul_q8(t->hb, t->xq, fs, &t->w1, l * hid, hid);
        matmul_q8(t->hb2, t->xq, fs, &t->w3, l * hid, hid);
        PROF_MARK(PROF_MM_FFN);

        for (int i = 0; i < hid; i++) {
            float val = t->hb[i];
            val *= 1.0f / (1.0f + expf_fast(-val));
            t->hb[i] = val * t->hb2[i];
        }
        PROF_MARK(PROF_SWIGLU);

        const float hs = quantize_vec(t->hq, t->hb, hid, npad_h);
        PROF_MARK(PROF_QUANT);
        matmul_q8(t->xb, t->hq, hs, &t->w2, l * dim, dim);
        PROF_MARK(PROF_MM_FFN);

        for (int i = 0; i < dim; i++) x[i] += t->xb[i];
        PROF_MARK(PROF_OTHER);
    }

    rmsnorm(x, x, t->rms_final, dim);
    PROF_MARK(PROF_RMSNORM);

    // classifier: the shared tokens table doubles as wcls
    const float cs = quantize_vec(t->xq, x, dim, npad_d);
    PROF_MARK(PROF_QUANT);
    matmul_q8(t->logits, t->xq, cs, &t->tokens, 0, c->vocab_size);
    PROF_MARK(PROF_MM_CLS);
    return t->logits;
}
