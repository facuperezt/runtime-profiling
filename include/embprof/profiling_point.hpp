#ifndef EMBPROF_PROFILING_POINT_HPP
#define EMBPROF_PROFILING_POINT_HPP

/// @file profiling_point.hpp
/// @brief A single profiling measurement point combining stats + histogram + quantiles.
///
/// This is the main user-facing type: create one per function/section you want
/// to profile, then use scoped_timer or manual start/stop to feed it.

#include "config.hpp"
#include "clock.hpp"
#include "running_stats.hpp"
#include "histogram.hpp"
#include "p2_quantile.hpp"

#include <cstdint>

namespace embprof {

/// @tparam HistBuckets  Number of histogram buckets.
/// @tparam Clock        Clock source (must have static `now()`).
template <uint32_t HistBuckets = EMBPROF_DEFAULT_HISTOGRAM_BUCKETS,
          typename Clock = default_clock>
class profiling_point {
public:
    /// @param name       Human-readable label (pointer must remain valid).
    /// @param hist_lo    Histogram lower bound (in ticks).
    /// @param hist_hi    Histogram upper bound (in ticks).
    /// @param hist_mode  Linear or log-linear bucket spacing.
    profiling_point(const char* name,
                    float_t hist_lo,
                    float_t hist_hi,
                    bucket_mode hist_mode = bucket_mode::log_linear) noexcept
        : name_(name)
        , hist_(hist_lo, hist_hi, hist_mode)
        , p50_(0.5)
        , p90_(0.90)
        , p99_(0.99)
    {}

    /// Record an already-computed elapsed time (in ticks).
    void record(float_t elapsed) noexcept {
        stats_.push(elapsed);
        hist_.record(elapsed);
        p50_.push(elapsed);
        p90_.push(elapsed);
        p99_.push(elapsed);
    }

    /// Manual start — call stop() later.
    void start() noexcept {
        start_tick_ = Clock::now();
    }

    /// Manual stop — records elapsed since start().
    void stop() noexcept {
        tick_t end = Clock::now();
        float_t elapsed = compute_elapsed(start_tick_, end);
        record(elapsed);
    }

    // -- Accessors -----------------------------------------------------------

    EMBPROF_NODISCARD const char* name() const noexcept { return name_; }

    EMBPROF_NODISCARD const running_stats&            stats()     const noexcept { return stats_; }
    EMBPROF_NODISCARD const histogram<HistBuckets>&   hist()      const noexcept { return hist_; }
    EMBPROF_NODISCARD const p2_quantile&              quantile50() const noexcept { return p50_; }
    EMBPROF_NODISCARD const p2_quantile&              quantile90() const noexcept { return p90_; }
    EMBPROF_NODISCARD const p2_quantile&              quantile99() const noexcept { return p99_; }

    // Mutable access for restore
    running_stats&          stats_mut()  noexcept { return stats_; }
    histogram<HistBuckets>& hist_mut()   noexcept { return hist_; }
    p2_quantile&            p50_mut()    noexcept { return p50_; }
    p2_quantile&            p90_mut()    noexcept { return p90_; }
    p2_quantile&            p99_mut()    noexcept { return p99_; }

    void reset() noexcept {
        stats_.reset();
        hist_.reset();
        p50_ = p2_quantile(0.5);
        p90_ = p2_quantile(0.90);
        p99_ = p2_quantile(0.99);
    }

    // -- Compact state for serialization --------------------------------------
    struct state_t {
        running_stats::state_t              stats;
        typename histogram<HistBuckets>::state_t hist;
        p2_quantile::state_t                p50;
        p2_quantile::state_t                p90;
        p2_quantile::state_t                p99;
    };

    EMBPROF_NODISCARD state_t snapshot() const noexcept {
        return {
            stats_.snapshot(),
            hist_.snapshot(),
            p50_.snapshot(),
            p90_.snapshot(),
            p99_.snapshot()
        };
    }

    void restore(const state_t& s) noexcept {
        stats_.restore(s.stats);
        hist_.restore(s.hist);
        p50_.restore(s.p50);
        p90_.restore(s.p90);
        p99_.restore(s.p99);
    }

private:
    static float_t compute_elapsed(tick_t start, tick_t end) noexcept {
        // Handle down-counting timers (e.g. SysTick)
        tick_t raw = (end >= start) ? (end - start) : (start - end);
        return static_cast<float_t>(raw);
    }

    const char*            name_;
    tick_t                 start_tick_ = 0;
    running_stats          stats_;
    histogram<HistBuckets> hist_;
    p2_quantile            p50_;
    p2_quantile            p90_;
    p2_quantile            p99_;
};

// ---------------------------------------------------------------------------
// RAII scoped timer
// ---------------------------------------------------------------------------

/// Measures elapsed time from construction to destruction.
/// Usage:
///   profiling_point<> my_point("foo", 0, 100000);
///   { embprof::scoped_timer timer(my_point); do_work(); }
template <uint32_t HistBuckets = EMBPROF_DEFAULT_HISTOGRAM_BUCKETS,
          typename Clock = default_clock>
class scoped_timer {
public:
    explicit scoped_timer(profiling_point<HistBuckets, Clock>& pp) noexcept
        : pp_(pp), start_(Clock::now()) {}

    ~scoped_timer() noexcept {
        tick_t end = Clock::now();
        float_t elapsed = (end >= start_)
            ? static_cast<float_t>(end - start_)
            : static_cast<float_t>(start_ - end);
        pp_.record(elapsed);
    }

    // Non-copyable, non-movable
    scoped_timer(const scoped_timer&)            = delete;
    scoped_timer& operator=(const scoped_timer&) = delete;
    scoped_timer(scoped_timer&&)                 = delete;
    scoped_timer& operator=(scoped_timer&&)      = delete;

private:
    profiling_point<HistBuckets, Clock>& pp_;
    tick_t start_;
};

} // namespace embprof

#endif // EMBPROF_PROFILING_POINT_HPP
