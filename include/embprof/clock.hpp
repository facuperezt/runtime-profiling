#ifndef EMBPROF_CLOCK_HPP
#define EMBPROF_CLOCK_HPP

/// @file clock.hpp
/// @brief Pluggable clock abstraction.
///
/// Provide your own clock by defining a struct that satisfies the EmbprofClock
/// concept (a static `now()` returning embprof::tick_t), then alias or
/// specialise embprof::default_clock.

#include "config.hpp"

#include <cstdint>

// std::chrono must be included OUTSIDE the embprof namespace to avoid
// pulling standard library internals into it.
#if !defined(EMBPROF_NO_STD_CHRONO)
#include <chrono>
#endif

namespace embprof {

using tick_t  = EMBPROF_TICK_TYPE;
using float_t = EMBPROF_FLOAT_TYPE;

// ---------------------------------------------------------------------------
// Built-in clocks
// ---------------------------------------------------------------------------

/// ARM DWT cycle counter (Cortex-M3/M4/M7).
/// You must enable the DWT yourself before first use:
///   CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
///   DWT->CYCCNT = 0;
///   DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
struct dwt_clock {
    static tick_t now() noexcept {
        // DWT->CYCCNT is at address 0xE0001004
        return *reinterpret_cast<volatile const uint32_t*>(0xE0001004u);
    }
};

/// SysTick-based clock (reads the current value register).
/// Note: SysTick counts DOWN, so the timer wrapper handles inversion.
struct systick_clock {
    static tick_t now() noexcept {
        // SysTick->VAL is at address 0xE000E018
        return *reinterpret_cast<volatile const uint32_t*>(0xE000E018u);
    }
    /// SysTick counts down — elapsed = start - stop (mod reload).
    static constexpr bool counts_down = true;
};

/// Fallback: user must provide their own clock.
/// This stub always returns 0 — it exists so the library compiles on desktop
/// for unit-testing without hardware.
struct null_clock {
    static tick_t now() noexcept { return 0; }
};

// ---------------------------------------------------------------------------
// std::chrono clock for desktop / unit testing
// ---------------------------------------------------------------------------
#if !defined(EMBPROF_NO_STD_CHRONO)
struct chrono_clock {
    static tick_t now() noexcept {
        auto n = ::std::chrono::duration_cast<::std::chrono::nanoseconds>(
                     ::std::chrono::steady_clock::now().time_since_epoch())
                     .count();
        return static_cast<tick_t>(n);
    }
};
#endif // EMBPROF_NO_STD_CHRONO

// ---------------------------------------------------------------------------
// Default clock selection
// ---------------------------------------------------------------------------
#if defined(EMBPROF_CLOCK_DWT)
    using default_clock = dwt_clock;
#elif defined(EMBPROF_CLOCK_SYSTICK)
    using default_clock = systick_clock;
#elif defined(EMBPROF_CLOCK_USER)
    // User must define `struct embprof::user_clock` before including this header
    // or provide a full specialisation.
    using default_clock = user_clock;
#elif !defined(EMBPROF_NO_STD_CHRONO)
    using default_clock = chrono_clock;
#else
    using default_clock = null_clock;
#endif

} // namespace embprof

#endif // EMBPROF_CLOCK_HPP
