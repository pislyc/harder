// Flush denormals to zero on the calling thread, like the plugin's audio callback (x86: MXCSR FTZ | DAZ,
// AArch64: FPCR.FZ, which flushes inputs and results).
#pragma once
#include <cstdint>
#if defined(__SSE__) || defined(__x86_64__)
#include <xmmintrin.h>
#include <pmmintrin.h>
static inline void harderFtz() { _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON); _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON); }
#elif defined(__aarch64__)
static inline void harderFtz() { uint64_t v; __asm__ __volatile__("mrs %0, fpcr" : "=r"(v)); v |= 1ull << 24; __asm__ __volatile__("msr fpcr, %0" : : "r"(v)); }
#else
static inline void harderFtz() {}
#endif
