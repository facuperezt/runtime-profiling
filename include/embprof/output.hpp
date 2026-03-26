#ifndef EMBPROF_OUTPUT_HPP
#define EMBPROF_OUTPUT_HPP

/// @file output.hpp
/// @brief Sink abstraction for profiling output — ITM, callback, null.
///
/// All sinks provide put_char(char) and write(const char*, uint32_t).
/// Use templates for zero-overhead static dispatch on embedded targets.

#include "config.hpp"

#include <cstdint>

namespace embprof {

// ---------------------------------------------------------------------------
// ITM sink — writes to ARM ITM stimulus port via raw register addresses.
// On non-ARM targets, this is a no-op.
// ---------------------------------------------------------------------------

template <uint8_t Port = 0>
struct itm_sink {
    static_assert(Port < 32, "ITM port must be 0-31");

    static void put_char(char c) noexcept {
#if defined(__ARM_ARCH) || defined(EMBPROF_HAS_ITM)
        // Raw ITM register addresses (no CMSIS dependency)
        constexpr uint32_t ITM_BASE = 0xE0000000u;
        constexpr uint32_t ITM_PORT = ITM_BASE + 4u * Port;
        constexpr uint32_t ITM_TCR  = 0xE0000E80u;  // Trace Control Register
        constexpr uint32_t ITM_TER  = 0xE0000E00u;  // Trace Enable Register

        volatile uint32_t* tcr = reinterpret_cast<volatile uint32_t*>(ITM_TCR);
        volatile uint32_t* ter = reinterpret_cast<volatile uint32_t*>(ITM_TER);
        volatile uint8_t*  port = reinterpret_cast<volatile uint8_t*>(ITM_PORT);

        // Check ITM enabled (TCR bit 0) and port enabled (TER bit N)
        if ((*tcr & 1u) == 0) return;
        if ((*ter & (1u << Port)) == 0) return;

        // Busy-wait with timeout for port ready
        // PORT register reads non-zero when ready to accept data
        constexpr uint32_t TIMEOUT = 100000u;
        for (uint32_t i = 0; i < TIMEOUT; ++i) {
            volatile uint32_t* port32 = reinterpret_cast<volatile uint32_t*>(ITM_PORT);
            if (*port32 != 0) {
                *port = static_cast<uint8_t>(c);
                return;
            }
        }
#else
        (void)c;
#endif
    }

    static void write(const char* s, uint32_t len) noexcept {
#if defined(__ARM_ARCH) || defined(EMBPROF_HAS_ITM)
        for (uint32_t i = 0; i < len; ++i) {
            put_char(s[i]);
        }
#else
        (void)s;
        (void)len;
#endif
    }
};

// ---------------------------------------------------------------------------
// Null sink — discards everything. Used when EMBPROF_DISABLE=1.
// ---------------------------------------------------------------------------

struct null_sink {
    static void put_char(char) noexcept {}
    static void write(const char*, uint32_t) noexcept {}
};

// ---------------------------------------------------------------------------
// Callback sink — wraps a user-provided function pointer.
// ---------------------------------------------------------------------------

class callback_sink {
public:
    using write_fn = void(*)(const char* data, uint32_t len);

    explicit callback_sink(write_fn fn) noexcept : fn_(fn) {}

    void put_char(char c) noexcept { if (fn_) fn_(&c, 1); }
    void write(const char* s, uint32_t len) noexcept { if (fn_) fn_(s, len); }

private:
    write_fn fn_;
};

} // namespace embprof

#endif // EMBPROF_OUTPUT_HPP
