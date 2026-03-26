#ifndef EMBPROF_CONFIG_HPP
#define EMBPROF_CONFIG_HPP

/// @file config.hpp
/// @brief Compile-time configuration for embprof.
///
/// Override any of these before including embprof headers, or define them
/// via compiler flags / CMake options.

// ---------------------------------------------------------------------------
// C++ standard detection
// ---------------------------------------------------------------------------
#if __cplusplus >= 201703L
#   define EMBPROF_HAS_CPP17 1
#else
#   define EMBPROF_HAS_CPP17 0
#endif

// ---------------------------------------------------------------------------
// Optional features (opt-in via CMake or -D flags)
// ---------------------------------------------------------------------------

/// Maximum number of profiling points the global registry can hold.
/// Each slot costs ~sizeof(void*). Set to 0 to disable the registry entirely.
#ifndef EMBPROF_MAX_PROFILING_POINTS
#   define EMBPROF_MAX_PROFILING_POINTS 32
#endif

/// Default number of histogram buckets when none is specified at construction.
#ifndef EMBPROF_DEFAULT_HISTOGRAM_BUCKETS
#   define EMBPROF_DEFAULT_HISTOGRAM_BUCKETS 20
#endif

/// When defined to 1, all profiling macros expand to nothing.
#ifndef EMBPROF_DISABLE
#   define EMBPROF_DISABLE 0
#endif

/// Type used for time values (ticks or nanoseconds, depending on the clock).
/// Must be an unsigned integral type.
#ifndef EMBPROF_TICK_TYPE
#   define EMBPROF_TICK_TYPE uint32_t
#endif

/// Floating-point type for statistics accumulators.
/// Use float on very constrained targets to halve RAM per profiling point.
#ifndef EMBPROF_FLOAT_TYPE
#   define EMBPROF_FLOAT_TYPE double
#endif

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------
#if EMBPROF_HAS_CPP17
#   define EMBPROF_INLINE_VAR inline
#   define EMBPROF_IF_CONSTEXPR if constexpr
#   define EMBPROF_NODISCARD [[nodiscard]]
#else
#   define EMBPROF_INLINE_VAR
#   define EMBPROF_IF_CONSTEXPR if
#   define EMBPROF_NODISCARD
#endif

#endif // EMBPROF_CONFIG_HPP
