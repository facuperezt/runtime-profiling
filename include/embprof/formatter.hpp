#ifndef EMBPROF_FORMATTER_HPP
#define EMBPROF_FORMATTER_HPP

/// @file formatter.hpp
/// @brief Lightweight text formatting and report generation for profiling data.
///
/// No sprintf/snprintf/iostream — all formatting is hand-rolled for bare-metal.
/// All functions are templated on Sink for zero-overhead static dispatch.

#include "config.hpp"
#include "profiling_point.hpp"
#include "registry.hpp"

#include <cstdint>
#include <cmath>

namespace embprof {
namespace fmt {

// ---------------------------------------------------------------------------
// Core formatting primitives
// ---------------------------------------------------------------------------

/// Format an unsigned 32-bit integer in decimal.
template <typename Sink>
void write_u32(Sink& sink, uint32_t value) {
    if (value == 0) {
        sink.put_char('0');
        return;
    }
    // Max uint32_t is 4294967295 — 10 digits
    char buf[10];
    uint32_t pos = 0;
    while (value > 0) {
        buf[pos++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    }
    // Write in reverse order
    for (uint32_t i = pos; i > 0; --i) {
        sink.put_char(buf[i - 1]);
    }
}

/// Format a signed 32-bit integer in decimal.
template <typename Sink>
void write_i32(Sink& sink, int32_t value) {
    if (value < 0) {
        sink.put_char('-');
        // Handle INT32_MIN carefully: -(-2147483648) overflows int32_t
        // so cast to uint32_t first.
        if (value == (-2147483647 - 1)) {
            // INT32_MIN = -2147483648
            write_u32(sink, static_cast<uint32_t>(2147483648u));
            return;
        }
        write_u32(sink, static_cast<uint32_t>(-value));
    } else {
        write_u32(sink, static_cast<uint32_t>(value));
    }
}

/// Format a float with a configurable number of decimal places.
/// No sprintf — multiplies by 10^decimals and formats integer + fractional parts.
template <typename Sink>
void write_float(Sink& sink, float_t value, uint8_t decimals = 2) {
    // Handle negative
    if (value < 0) {
        sink.put_char('-');
        value = -value;
    }

    // Handle NaN/Inf
    if (std::isnan(value)) {
        sink.write("nan", 3);
        return;
    }
    if (std::isinf(value)) {
        sink.write("inf", 3);
        return;
    }

    // Compute multiplier for fractional part: 10^decimals
    uint32_t mult = 1;
    for (uint8_t i = 0; i < decimals; ++i) {
        mult *= 10;
    }

    // Round and split into integer + fractional
    // Add 0.5 / mult for rounding before truncation
    float_t rounded = value + 0.5 / static_cast<float_t>(mult);
    uint32_t int_part = static_cast<uint32_t>(rounded);
    float_t frac_f = (rounded - static_cast<float_t>(int_part)) * static_cast<float_t>(mult);
    uint32_t frac_part = static_cast<uint32_t>(frac_f);

    // Clamp fractional part (rounding edge case)
    if (frac_part >= mult) {
        frac_part = mult - 1;
    }

    write_u32(sink, int_part);

    if (decimals > 0) {
        sink.put_char('.');
        // Write leading zeros for fractional part
        uint32_t leading = mult / 10;
        while (leading > 0 && frac_part < leading) {
            sink.put_char('0');
            leading /= 10;
        }
        if (frac_part > 0) {
            write_u32(sink, frac_part);
        }
    }
}

/// Write a null-terminated string.
template <typename Sink>
void write_str(Sink& sink, const char* s) {
    if (!s) return;
    while (*s) {
        sink.put_char(*s);
        ++s;
    }
}

/// Write a newline character.
template <typename Sink>
void write_newline(Sink& sink) {
    sink.put_char('\n');
}

} // namespace fmt

// ---------------------------------------------------------------------------
// High-level report functions
// ---------------------------------------------------------------------------

/// Full multi-line report for a single profiling point.
template <typename Sink, uint32_t N, typename Clock>
void report(Sink& sink, const profiling_point<N, Clock>& pp) {
    const auto& s = pp.stats();
    const auto& h = pp.hist();

    // Header: [name]
    fmt::write_str(sink, "[");
    fmt::write_str(sink, pp.name());
    fmt::write_str(sink, "]");
    fmt::write_newline(sink);

    // Count
    fmt::write_str(sink, "  count : ");
    fmt::write_u32(sink, s.count());
    fmt::write_newline(sink);

    if (s.count() == 0) {
        fmt::write_str(sink, "  (no data)");
        fmt::write_newline(sink);
        return;
    }

    // Mean
    fmt::write_str(sink, "  mean  : ");
    fmt::write_float(sink, s.mean());
    fmt::write_newline(sink);

    // Min
    fmt::write_str(sink, "  min   : ");
    fmt::write_float(sink, s.min_val());
    fmt::write_newline(sink);

    // Max
    fmt::write_str(sink, "  max   : ");
    fmt::write_float(sink, s.max_val());
    fmt::write_newline(sink);

    // Stddev
    fmt::write_str(sink, "  stddev: ");
    fmt::write_float(sink, s.stddev());
    fmt::write_newline(sink);

    // Quantiles (P² estimates)
    fmt::write_str(sink, "  p50   : ");
    fmt::write_float(sink, pp.quantile50().get());
    fmt::write_newline(sink);

    fmt::write_str(sink, "  p90   : ");
    fmt::write_float(sink, pp.quantile90().get());
    fmt::write_newline(sink);

    fmt::write_str(sink, "  p99   : ");
    fmt::write_float(sink, pp.quantile99().get());
    fmt::write_newline(sink);

    // Histogram
    fmt::write_str(sink, "  --- histogram (");
    fmt::write_u32(sink, h.bucket_count());
    fmt::write_str(sink, " buckets) ---");
    fmt::write_newline(sink);

    // Underflow
    fmt::write_str(sink, "        <");
    fmt::write_float(sink, h.bucket_lo(0));
    fmt::write_str(sink, ": ");
    fmt::write_u32(sink, h.underflow());
    fmt::write_newline(sink);

    // Buckets
    for (uint32_t i = 0; i < h.bucket_count(); ++i) {
        fmt::write_str(sink, "  [");
        fmt::write_float(sink, h.bucket_lo(i));
        fmt::write_str(sink, ", ");
        fmt::write_float(sink, h.bucket_hi(i));
        fmt::write_str(sink, "): ");
        fmt::write_u32(sink, h.bucket(i));
        fmt::write_newline(sink);
    }

    // Overflow
    fmt::write_str(sink, "       >=");
    fmt::write_float(sink, h.bucket_lo(h.bucket_count()));
    fmt::write_str(sink, ": ");
    fmt::write_u32(sink, h.overflow());
    fmt::write_newline(sink);
}

/// Single-line summary for a profiling point.
template <typename Sink, uint32_t N, typename Clock>
void report_summary(Sink& sink, const profiling_point<N, Clock>& pp) {
    const auto& s = pp.stats();

    fmt::write_str(sink, "[");
    fmt::write_str(sink, pp.name());
    fmt::write_str(sink, "] n=");
    fmt::write_u32(sink, s.count());
    fmt::write_str(sink, " mean=");
    fmt::write_float(sink, s.mean());
    fmt::write_str(sink, " p50=");
    fmt::write_float(sink, pp.quantile50().get());
    fmt::write_str(sink, " p90=");
    fmt::write_float(sink, pp.quantile90().get());
    fmt::write_str(sink, " p99=");
    fmt::write_float(sink, pp.quantile99().get());
    fmt::write_newline(sink);
}

/// Report all registered profiling points.
template <typename Sink>
void report_all(Sink& sink) {
#if EMBPROF_MAX_PROFILING_POINTS > 0
    registry::instance().for_each([&sink](profiling_point_base& pp) {
        const auto& s = pp.stats();

        fmt::write_str(sink, "[");
        fmt::write_str(sink, pp.name());
        fmt::write_str(sink, "]");
        fmt::write_newline(sink);

        fmt::write_str(sink, "  count : ");
        fmt::write_u32(sink, s.count());
        fmt::write_newline(sink);

        if (s.count() > 0) {
            fmt::write_str(sink, "  mean  : ");
            fmt::write_float(sink, s.mean());
            fmt::write_newline(sink);

            fmt::write_str(sink, "  min   : ");
            fmt::write_float(sink, s.min_val());
            fmt::write_newline(sink);

            fmt::write_str(sink, "  max   : ");
            fmt::write_float(sink, s.max_val());
            fmt::write_newline(sink);

            fmt::write_str(sink, "  stddev: ");
            fmt::write_float(sink, s.stddev());
            fmt::write_newline(sink);

            fmt::write_str(sink, "  p50   : ");
            fmt::write_float(sink, pp.p50());
            fmt::write_newline(sink);

            fmt::write_str(sink, "  p90   : ");
            fmt::write_float(sink, pp.p90());
            fmt::write_newline(sink);

            fmt::write_str(sink, "  p99   : ");
            fmt::write_float(sink, pp.p99());
            fmt::write_newline(sink);
        } else {
            fmt::write_str(sink, "  (no data)");
            fmt::write_newline(sink);
        }
    });
#else
    (void)sink;
#endif
}

} // namespace embprof

#endif // EMBPROF_FORMATTER_HPP
