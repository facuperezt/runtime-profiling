#ifndef EMBPROF_HISTOGRAM_HPP
#define EMBPROF_HISTOGRAM_HPP

/// @file histogram.hpp
/// @brief Fixed-memory histogram with linear or log-linear bucket spacing.
///
/// Template parameter N controls the number of buckets (compile-time).
/// All memory is stack-allocated — no heap, no exceptions.

#include "config.hpp"
#include "clock.hpp"

#include <cstdint>
#include <cmath>
#include <algorithm>

namespace embprof {

/// Bucket spacing strategy.
enum class bucket_mode : uint8_t {
    linear,      ///< Equal-width buckets from [lo, hi).
    log_linear   ///< Log-spaced buckets — good for latency distributions.
};

/// @tparam N  Number of buckets (plus underflow + overflow = N+2 counters).
template <uint32_t N = EMBPROF_DEFAULT_HISTOGRAM_BUCKETS>
class histogram {
    static_assert(N >= 2, "Need at least 2 buckets");

public:
    /// Construct a linear histogram over [lo, hi).
    histogram(float_t lo, float_t hi) noexcept
        : mode_(bucket_mode::linear)
        , lo_(lo)
        , hi_(hi)
        , log_base_(0)
    {
        reset();
        compute_boundaries_linear();
    }

    /// Construct a log-linear histogram.
    /// Buckets span [lo, hi) with logarithmic spacing (base e by default).
    histogram(float_t lo, float_t hi, bucket_mode mode) noexcept
        : mode_(mode)
        , lo_(lo)
        , hi_(hi)
        , log_base_(0)
    {
        reset();
        if (mode_ == bucket_mode::log_linear) {
            compute_boundaries_log();
        } else {
            compute_boundaries_linear();
        }
    }

    void reset() noexcept {
        for (uint32_t i = 0; i < N + 2; ++i) {
            counts_[i] = 0;
        }
        total_count_ = 0;
    }

    /// Record a value into the appropriate bucket.
    void record(float_t value) noexcept {
        ++total_count_;

        if (value < lo_) {
            ++counts_[0]; // underflow
            return;
        }
        if (value >= hi_) {
            ++counts_[N + 1]; // overflow
            return;
        }

        // Binary search for the right bucket.
        // boundaries_[0] = lo_, boundaries_[N] = hi_
        // Bucket i (1-based in counts_) covers [boundaries_[i-1], boundaries_[i])
        uint32_t left  = 0;
        uint32_t right = N;
        while (left < right) {
            uint32_t mid = left + (right - left) / 2;
            if (value >= boundaries_[mid + 1]) {
                left = mid + 1;
            } else {
                right = mid;
            }
        }
        ++counts_[left + 1]; // +1 because counts_[0] is underflow
    }

    // -- Accessors -----------------------------------------------------------

    EMBPROF_NODISCARD uint32_t total_count() const noexcept { return total_count_; }

    /// Number of user buckets (excludes underflow/overflow).
    static constexpr uint32_t bucket_count() noexcept { return N; }

    /// Underflow count (values < lo).
    EMBPROF_NODISCARD uint32_t underflow() const noexcept { return counts_[0]; }

    /// Overflow count (values >= hi).
    EMBPROF_NODISCARD uint32_t overflow()  const noexcept { return counts_[N + 1]; }

    /// Count in bucket i (0-based, i < N).
    EMBPROF_NODISCARD uint32_t bucket(uint32_t i) const noexcept {
        return (i < N) ? counts_[i + 1] : 0;
    }

    /// Lower boundary of bucket i.
    EMBPROF_NODISCARD float_t bucket_lo(uint32_t i) const noexcept {
        return (i < N) ? boundaries_[i] : hi_;
    }

    /// Upper boundary of bucket i.
    EMBPROF_NODISCARD float_t bucket_hi(uint32_t i) const noexcept {
        return (i <= N) ? boundaries_[i + 1] : hi_;
    }

    /// Estimate the p-quantile (0 < p < 1) from histogram counts.
    /// This is an interpolation — accuracy depends on bucket granularity.
    EMBPROF_NODISCARD float_t quantile(float_t p) const noexcept {
        if (total_count_ == 0) return 0;

        const float_t target = p * static_cast<float_t>(total_count_);
        float_t cumulative = static_cast<float_t>(counts_[0]); // underflow

        if (cumulative >= target) return lo_;

        for (uint32_t i = 0; i < N; ++i) {
            const float_t prev = cumulative;
            cumulative += static_cast<float_t>(counts_[i + 1]);
            if (cumulative >= target) {
                // Linear interpolation within the bucket
                const float_t frac = (counts_[i + 1] > 0)
                    ? (target - prev) / static_cast<float_t>(counts_[i + 1])
                    : static_cast<float_t>(0.5);
                return boundaries_[i] + frac * (boundaries_[i + 1] - boundaries_[i]);
            }
        }
        return hi_;
    }

    // -- Serialization -------------------------------------------------------

    struct state_t {
        bucket_mode mode;
        float_t     lo;
        float_t     hi;
        uint32_t    total_count;
        uint32_t    counts[N + 2];
        // Boundaries can be recomputed from lo, hi, mode — no need to serialize.
    };

    EMBPROF_NODISCARD state_t snapshot() const noexcept {
        state_t s;
        s.mode        = mode_;
        s.lo          = lo_;
        s.hi          = hi_;
        s.total_count = total_count_;
        for (uint32_t i = 0; i < N + 2; ++i) {
            s.counts[i] = counts_[i];
        }
        return s;
    }

    void restore(const state_t& s) noexcept {
        mode_        = s.mode;
        lo_          = s.lo;
        hi_          = s.hi;
        total_count_ = s.total_count;
        for (uint32_t i = 0; i < N + 2; ++i) {
            counts_[i] = s.counts[i];
        }
        if (mode_ == bucket_mode::log_linear) {
            compute_boundaries_log();
        } else {
            compute_boundaries_linear();
        }
    }

    /// Merge another histogram (must have same lo, hi, mode, N).
    void merge(const histogram& other) noexcept {
        total_count_ += other.total_count_;
        for (uint32_t i = 0; i < N + 2; ++i) {
            counts_[i] += other.counts_[i];
        }
    }

private:
    void compute_boundaries_linear() noexcept {
        const float_t step = (hi_ - lo_) / static_cast<float_t>(N);
        for (uint32_t i = 0; i <= N; ++i) {
            boundaries_[i] = lo_ + static_cast<float_t>(i) * step;
        }
    }

    void compute_boundaries_log() noexcept {
        // Log-spaced: boundaries_[i] = lo * (hi/lo)^(i/N)
        // Requires lo > 0.
        const float_t log_lo = std::log(lo_ > 0 ? lo_ : static_cast<float_t>(1));
        const float_t log_hi = std::log(hi_ > 0 ? hi_ : static_cast<float_t>(2));
        const float_t step   = (log_hi - log_lo) / static_cast<float_t>(N);
        for (uint32_t i = 0; i <= N; ++i) {
            boundaries_[i] = std::exp(log_lo + static_cast<float_t>(i) * step);
        }
        // Snap edges exactly.
        boundaries_[0] = lo_;
        boundaries_[N] = hi_;
    }

    bucket_mode mode_;
    float_t     lo_;
    float_t     hi_;
    float_t     log_base_;
    float_t     boundaries_[N + 1]; // N+1 fence posts
    uint32_t    counts_[N + 2];     // [underflow][bucket 0]..[bucket N-1][overflow]
    uint32_t    total_count_;
};

} // namespace embprof

#endif // EMBPROF_HISTOGRAM_HPP
