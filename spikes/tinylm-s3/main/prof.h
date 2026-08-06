// Shared cycle-count profiling for the inference kernels (fp32 and int8).
// esp_cpu_get_cycle_count() is one special-register read, so a mark costs
// nothing next to the sections it times. Deltas are 32-bit (the counter wraps
// every ~17.9 s at 240 MHz, far longer than any token); accumulators are 64.
//
// Mark style: each PROF_MARK attributes everything since the previous mark
// (or PROF_START) to one bucket, then restarts the clock. forward() is fully
// sequential, so this covers 100% of it with no gaps and no double counting.
#pragma once
#include <stdint.h>
#include "esp_cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { PROF_MM_QKV, PROF_MM_WO, PROF_MM_FFN, PROF_MM_CLS,
       PROF_ATTN, PROF_RMSNORM, PROF_ROPE, PROF_SWIGLU, PROF_QUANT,
       PROF_OTHER, PROF_N };
extern uint64_t prof_cycles[PROF_N];

#define PROF_START() uint32_t prof_c_ = esp_cpu_get_cycle_count()
#define PROF_MARK(slot) do { uint32_t prof_n_ = esp_cpu_get_cycle_count(); \
    prof_cycles[slot] += prof_n_ - prof_c_; prof_c_ = prof_n_; } while (0)

void prof_reset(void);
void prof_report(int n_tokens);

#ifdef __cplusplus
}
#endif
