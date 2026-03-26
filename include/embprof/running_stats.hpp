#ifndef EMBPROF_RUNNING_STATS_HPP
#define EMBPROF_RUNNING_STATS_HPP

/// @file running_stats.hpp
/// @brief Online (streaming) statistics using Welford's algorithm.
///
/// Tracks count, min, max, mean, variance, and standard deviation without
/// storing any individual observations.  Fixed memory: ~40-48 bytes depending
/// on float_t.

#include "config.hpp"
#include "clock.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <algorithm>

namespace embprof {

class running_stats {
public:
    running_stats() noexcept { reset(); }

    void reset() noexcept {
        count_ = 0;
        min_   = (std::numeric_limits<float_t>::max)();
        max_   = (std::numeric_limits<float_t>::lowest)();
        mean_  = 0;
        m2_    = 0;
        last_  = 0;
    }

    /// Record a single observation (in tick units).
    void push(float_t x) noexcept {
        ++count_;
        last_ = x;

        if (x < min_) min_ = x;
        if (x > max_) max_ = x;

        // Welford's online algorithm
        const float_t delta = x - mean_;
        mean_ += delta / static_cast<float_t>(count_);
        const float_t delta2 = x - mean_;
        m2_ += delta * delta2;
    }

    // -- Accessors -----------------------------------------------------------

    EMBPROF_NODISCARD uint32_t count()    const noexcept { return count_; }
    EMBPROF_NODISCARD float_t  min_val()  const noexcept { return count_ ? min_ : 0; }
    EMBPROF_NODISCARD float_t  max_val()  const noexcept { return count_ ? max_ : 0; }
    EMBPROF_NODISCARD float_t  mean()     const noexcept { return mean_; }
    EMBPROF_NODISCARD float_t  last()     const noexcept { return last_; }

    /// Population variance (biased).
    EMBPROF_NODISCARD float_t variance_population() const noexcept {
        return count_ > 0 ? m2_ / static_cast<float_t>(count_) : 0;
    }

    /// Sample variance (unbiased, Bessel's correction).
    EMBPROF_NODISCARD float_t variance() const noexcept {
        return count_ > 1 ? m2_ / static_cast<float_t>(count_ - 1) : 0;
    }

    /// Sample standard deviation.
    EMBPROF_NODISCARD float_t stddev() const noexcept {
        return std::sqrt(variance());
    }

    /// Population standard deviation.
    EMBPROF_NODISCARD float_t stddev_population() const noexcept {
        return std::sqrt(variance_population());
    }

    // -- Merge / serialization helpers ----------------------------------------

    /// Merge another running_stats into this one (parallel Welford).
    /// Useful after deserializing a remote state.
    void merge(const running_stats& other) noexcept {
        if (other.count_ == 0) return;
        if (count_ == 0) {
            *this = other;
            return;
        }

        const uint32_t combined_n = count_ + other.count_;
        const float_t delta = other.mean_ - mean_;
        const float_t new_mean = mean_ + delta * static_cast<float_t>(other.count_)
                                              / static_cast<float_t>(combined_n);

        // Chan's parallel algorithm for combining M2
        m2_ = m2_ + other.m2_
            + delta * delta
              * (static_cast<float_t>(count_) * static_cast<float_t>(other.count_))
              / static_cast<float_t>(combined_n);

        mean_  = new_mean;
        count_ = combined_n;
        min_   = (std::min)(min_, other.min_);
        max_   = (std::max)(max_, other.max_);
        last_  = other.last_;
    }

    // -- Raw state access for serialization -----------------------------------
    struct state_t {
        uint32_t count;
        float_t  min_val;
        float_t  max_val;
        float_t  mean;
        float_t  m2;
        float_t  last;
    };

    EMBPROF_NODISCARD state_t snapshot() const noexcept {
        return { count_, min_, max_, mean_, m2_, last_ };
    }

    void restore(const state_t& s) noexcept {
        count_ = s.count;
        min_   = s.min_val;
        max_   = s.max_val;
        mean_  = s.mean;
        m2_    = s.m2;
        last_  = s.last;
    }

private:
    uint32_t count_;
    float_t  min_;
    float_t  max_;
    float_t  mean_;
    float_t  m2_;
    float_t  last_;
};

} // namespace embprof

#endif // EMBPROF_RUNNING_STATS_HPP
