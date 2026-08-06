// Step 0 — can the ESP32-S3 run transformer inference at all, and how fast?
//
// This is the gate before any model training happens. The model here
// (stories260K) is a THROWAWAY BENCHMARK, not what we intend to ship: its
// output is near-gibberish and we do not care what it says, only how fast it
// says it. It is the right benchmark because it is small enough to run in
// fp32 with no quantisation kernel to write first, so this is a porting job
// rather than a research project.
//
// Measured, in order:
//   1. does esp_partition_mmap actually serve weights from flash
//   2. tokens/sec, and the per-token distribution (p50/p99 — jitter matters
//      more than the mean for something that shares a core with a face)
//   3. RAM cost: internal and PSRAM high-water
//   4. extrapolation to the model we would actually train
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <string>

extern "C" {
#include "run_api.h"
}

static const char* TAG = "tinylm";

namespace {

int cmp_i64(const void* a, const void* b) {
  const int64_t x = *static_cast<const int64_t*>(a), y = *static_cast<const int64_t*>(b);
  return (x > y) - (x < y);
}

void report_mem(const char* when) {
  ESP_LOGI(TAG, "%-22s internal free %7u   psram free %8u", when,
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

}  // namespace

extern "C" void app_main() {
  vTaskDelay(pdMS_TO_TICKS(300));
  ESP_LOGI(TAG, "=== Step 0: transformer inference on the S3 ===");
  report_mem("boot");

  Transformer t;
  build_transformer(&t, nullptr);  // weights come from the mmap'd partition
  ESP_LOGI(TAG, "model: dim=%d hidden=%d layers=%d heads=%d kv_heads=%d vocab=%d seq=%d",
           t.config.dim, t.config.hidden_dim, t.config.n_layers, t.config.n_heads,
           t.config.n_kv_heads, t.config.vocab_size, t.config.seq_len);
  report_mem("after build_transformer");

  Tokenizer tok;
  build_tokenizer(&tok, nullptr, t.config.vocab_size);
  Sampler sampler;
  build_sampler(&sampler, t.config.vocab_size, 1.0f, 0.9f, 1234567);
  report_mem("after tokenizer");

  // --- generate, timing every token ----------------------------------------
  // Each variant runs the same loop with the same RNG seed, so tok/s AND the
  // sample text are directly comparable across passes (quantisation may still
  // change the words — logits shift — but a broken kernel shows as garbage).
  auto run_pass = [&](const char* name, const std::function<float*(int, int)>& fwd) {
    ESP_LOGI(TAG, "=== pass: %s ===", name);
    sampler.rng_state = 1234567;
    prof_reset();
    constexpr int kSteps = 120;
    static int64_t dt[kSteps];
    int token = 1;  // BOS
    int pos = 0;
    const int64_t t_start = esp_timer_get_time();
    std::string out;
    while (pos < kSteps) {
      const int64_t a = esp_timer_get_time();
      float* logits = fwd(token, pos);
      const int next = sample(&sampler, logits);
      dt[pos] = esp_timer_get_time() - a;
      if (next == 1) break;  // BOS again = end
      char* piece = decode(&tok, token, next);
      if (piece && out.size() < 400) out += piece;
      token = next;
      pos++;
    }
    const int64_t total = esp_timer_get_time() - t_start;
    const int n = pos;

    qsort(dt, n, sizeof dt[0], cmp_i64);
    ESP_LOGI(TAG, "--- RESULT: %s ---------------------------------", name);
    ESP_LOGI(TAG, "generated %d tokens in %.0f ms", n, total / 1000.0);
    ESP_LOGI(TAG, "throughput      %.1f tok/s   (%.2f ms/token mean)",
             n * 1e6 / total, total / 1000.0 / n);
    ESP_LOGI(TAG, "per-token p50   %.2f ms", dt[n / 2] / 1000.0);
    ESP_LOGI(TAG, "per-token p99   %.2f ms", dt[(n * 99) / 100] / 1000.0);
    ESP_LOGI(TAG, "per-token min   %.2f ms   max %.2f ms", dt[0] / 1000.0, dt[n - 1] / 1000.0);
    report_mem("after generation");
    prof_report(n);

    // --- extrapolate to the model we would actually train ------------------
    // Compute scales with NON-embedding parameters; the embedding table is a
    // lookup, not a matmul. stories260K has 227,840 of them.
    const double ms = total / 1000.0 / n;
    const double per_param = ms / 227840.0;
    ESP_LOGI(TAG, "--- EXTRAPOLATION (same code path) --------------------");
    struct { const char* cname; long nonemb; } cfg[] = {
        {"Config S  v1024 d128 L6", 1179648},
        {"Config M  v2048 d192 L8", 3538944},
        {"Config L  v2048 d256 L8", 6291456},
    };
    for (auto& c : cfg) {
      const double p_ms = per_param * c.nonemb;
      ESP_LOGI(TAG, "%s -> %6.1f ms/token = %5.1f tok/s",
               c.cname, p_ms, 1000.0 / p_ms);
    }
    ESP_LOGI(TAG, "sample: %.120s", out.c_str());
  };

  auto fwd_fp32 = [&](int token, int pos) { return forward(&t, token, pos); };
  run_pass("fp32, weights mmap'd from flash", fwd_fp32);
  relocate_weights_to_psram(&t, 1056540);
  run_pass("fp32, weights in PSRAM", fwd_fp32);

  // int8 row-quantised kernel — avenue 2 (PSRAM) and avenue 3 (internal RAM).
  if (QTransformer* q8 = build_transformer_q8(MALLOC_CAP_SPIRAM)) {
    run_pass("int8 rowq8, weights in PSRAM",
             [&](int token, int pos) { return forward_q8(q8, token, pos); });
    free_transformer_q8(q8);
  }
  if (QTransformer* q8 = build_transformer_q8(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)) {
    run_pass("int8 rowq8, weights in INTERNAL RAM",
             [&](int token, int pos) { return forward_q8(q8, token, pos); });
    free_transformer_q8(q8);
  }

  for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
