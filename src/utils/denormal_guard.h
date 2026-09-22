#pragma once

// ---------------------------------------------------------------------------
// DenormalGuard
//
// Enables flush-to-zero (FTZ) and denormals-are-zero (DAZ) for the duration of
// a scope, restoring the previous mode on exit.
//
// Why this matters for a delay: the feedback path decays exponentially towards
// zero, so after the input stops the delay line fills with progressively
// smaller values. Once those values fall below the smallest normal float
// (~1.2e-38), the CPU switches to denormal arithmetic, which on x86 is handled
// by a microcode assist costing one to two orders of magnitude more than the
// equivalent normal-number operation. The audible result is silence; the
// measurable result is a sudden, apparently random spike in processing time
// during quiet passages and fade-outs.
//
// The mode is per-thread and is restored on scope exit, which matters here for
// a specific reason: CLAP's thread-check specification states that the
// audio-thread is symbolic, and that a host may schedule process() on
// different OS threads over time. Setting the bits once at activate() would
// therefore not reliably cover every thread the plugin runs on. The guard must
// be constructed inside process().
// ---------------------------------------------------------------------------

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || \
    (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define DELAY_DENORMAL_SSE 1
#include <xmmintrin.h>
#elif defined(__aarch64__) || defined(_M_ARM64)
#define DELAY_DENORMAL_ARM64 1
#endif

class DenormalGuard {
public:
    DenormalGuard() noexcept {
#if defined(DELAY_DENORMAL_SSE)
        previousState_ = _mm_getcsr();
        _mm_setcsr(previousState_ | 0x8000u | 0x0040u);
#elif defined(DELAY_DENORMAL_ARM64)
        __asm__ __volatile__("mrs %0, fpcr" : "=r"(previousState_));
        // Bit 24 = flush-to-zero.
        __asm__ __volatile__("msr fpcr, %0" ::"r"(previousState_ | (1ull << 24)));
#endif
    }

    ~DenormalGuard() noexcept {
#if defined(DELAY_DENORMAL_SSE)
        _mm_setcsr(previousState_);
#elif defined(DELAY_DENORMAL_ARM64)
        __asm__ __volatile__("msr fpcr, %0" ::"r"(previousState_));
#endif
    }

    DenormalGuard(const DenormalGuard&) = delete;
    DenormalGuard& operator=(const DenormalGuard&) = delete;

    static constexpr bool isSupported() noexcept {
#if defined(DELAY_DENORMAL_SSE) || defined(DELAY_DENORMAL_ARM64)
        return true;
#else
        return false;
#endif
    }

private:
#if defined(DELAY_DENORMAL_SSE)
    unsigned int previousState_ = 0;
#elif defined(DELAY_DENORMAL_ARM64)
    unsigned long long previousState_ = 0;
#endif
};
