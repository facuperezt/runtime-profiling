#ifndef EMBPROF_MACROS_HPP
#define EMBPROF_MACROS_HPP

/// @file macros.hpp
/// @brief Convenience macros for zero-overhead profiling.
///
/// Define EMBPROF_DISABLE=1 to compile out all profiling instrumentation.

#include "profiling_point.hpp"
#include "registry.hpp"

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------
#define EMBPROF_CONCAT_IMPL(a, b) a##b
#define EMBPROF_CONCAT(a, b) EMBPROF_CONCAT_IMPL(a, b)
#define EMBPROF_UNIQUE(prefix) EMBPROF_CONCAT(prefix, __LINE__)

#if EMBPROF_DISABLE

// All macros expand to nothing when profiling is disabled.
#define EMBPROF_POINT(name, lo, hi, ...)
#define EMBPROF_SCOPE(point)
#define EMBPROF_START(point)
#define EMBPROF_STOP(point)
#define EMBPROF_RECORD(point, elapsed)

#else // profiling enabled

/// Declare a static profiling point.
/// Usage: EMBPROF_POINT(my_func, 0, 100000);
///        EMBPROF_POINT(my_func, 0, 100000, embprof::bucket_mode::linear);
#define EMBPROF_POINT(var, lo, hi, ...)                                        \
    static ::embprof::profiling_point<> var(#var, lo, hi, ##__VA_ARGS__)

/// RAII-scoped measurement of the current block.
/// Usage: { EMBPROF_SCOPE(my_func); do_work(); }
#define EMBPROF_SCOPE(point)                                                   \
    ::embprof::scoped_timer<> EMBPROF_UNIQUE(_embprof_timer_)(point)

/// Manual start/stop.
#define EMBPROF_START(point) (point).start()
#define EMBPROF_STOP(point)  (point).stop()

/// Record a pre-computed elapsed time.
#define EMBPROF_RECORD(point, elapsed) (point).record(elapsed)

#endif // EMBPROF_DISABLE

#endif // EMBPROF_MACROS_HPP
