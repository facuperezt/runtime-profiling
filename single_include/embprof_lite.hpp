// embprof_lite.hpp — Minimal single-header runtime profiler for embedded C++14/17
//
// Drop this one file into your project. No dependencies beyond <cstdint>, <cmath>,
// <limits>, <algorithm>. Optional <chrono> for desktop testing (disable with
// EMBPROF_NO_STD_CHRONO).
//
// No heap. No exceptions. No RTTI. No external libraries.
//
// Usage:
//   #include "embprof_lite.hpp"
//
//   static embprof::profiling_point<> my_func("my_func", 100, 100000);
//
//   void foo() {
//       embprof::scoped_timer<> t(my_func);
//       // ... your code ...
//   }
//
//   // Later:
//   auto& s = my_func.stats();
//   printf("mean=%f p99=%f\n", s.mean(), my_func.quantile99().get());
//
// Full library with registry, serialization, ITM output, and macros:
//   https://github.com/facuperezt/runtime-profiling
//
// SPDX-License-Identifier: MIT

#ifndef EMBPROF_LITE_HPP
#define EMBPROF_LITE_HPP

#include <cstdint>
#include <cmath>
#include <limits>
#include <algorithm>

// ============================================================================
// Configuration
// ============================================================================

#if __cplusplus >= 201703L
#   define EMBPROF_NODISCARD [[nodiscard]]
#else
#   define EMBPROF_NODISCARD
#endif

#ifndef EMBPROF_TICK_TYPE
#   define EMBPROF_TICK_TYPE uint32_t
#endif

#ifndef EMBPROF_FLOAT_TYPE
#   define EMBPROF_FLOAT_TYPE double
#endif

#ifndef EMBPROF_DEFAULT_HISTOGRAM_BUCKETS
#   define EMBPROF_DEFAULT_HISTOGRAM_BUCKETS 20
#endif

// ============================================================================
// Clock abstraction
// ============================================================================

#if !defined(EMBPROF_NO_STD_CHRONO)
#include <chrono>
#endif

namespace embprof {

using tick_t  = EMBPROF_TICK_TYPE;
using float_t = EMBPROF_FLOAT_TYPE;

// -- Built-in clocks ---------------------------------------------------------

/// ARM DWT cycle counter (Cortex-M3/M4/M7).
/// Enable first:  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
///                DWT->CYCCNT = 0;  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
struct dwt_clock {
    static tick_t now() noexcept {
        return *reinterpret_cast<volatile const uint32_t*>(0xE0001004u);
    }
};

/// SysTick->VAL (counts down — library handles inversion).
struct systick_clock {
    static tick_t now() noexcept {
        return *reinterpret_cast<volatile const uint32_t*>(0xE000E018u);
    }
};

/// No-op clock — compiles on any target.
struct null_clock {
    static tick_t now() noexcept { return 0; }
};

#if !defined(EMBPROF_NO_STD_CHRONO)
/// Desktop clock for testing — wraps steady_clock to nanoseconds.
struct chrono_clock {
    static tick_t now() noexcept {
        auto n = ::std::chrono::duration_cast<::std::chrono::nanoseconds>(
                     ::std::chrono::steady_clock::now().time_since_epoch())
                     .count();
        return static_cast<tick_t>(n);
    }
};
#endif

// -- Default clock selection -------------------------------------------------
#if defined(EMBPROF_CLOCK_DWT)
    using default_clock = dwt_clock;
#elif defined(EMBPROF_CLOCK_SYSTICK)
    using default_clock = systick_clock;
#elif defined(EMBPROF_CLOCK_USER)
    using default_clock = user_clock; // you define embprof::user_clock
#elif !defined(EMBPROF_NO_STD_CHRONO)
    using default_clock = chrono_clock;
#else
    using default_clock = null_clock;
#endif

// ============================================================================
// Running statistics — Welford's online algorithm
// ============================================================================

class running_stats {
public:
    running_stats() noexcept { reset(); }

    void reset() noexcept {
        count_ = 0;
        min_   = (std::numeric_limits<float_t>::max)();
        max_   = (std::numeric_limits<float_t>::lowest)();
        mean_  = 0; m2_ = 0; last_ = 0;
    }

    void push(float_t x) noexcept {
        ++count_;
        last_ = x;
        if (x < min_) min_ = x;
        if (x > max_) max_ = x;
        const float_t d1 = x - mean_;
        mean_ += d1 / static_cast<float_t>(count_);
        const float_t d2 = x - mean_;
        m2_ += d1 * d2;
    }

    EMBPROF_NODISCARD uint32_t count()    const noexcept { return count_; }
    EMBPROF_NODISCARD float_t  min_val()  const noexcept { return count_ ? min_ : 0; }
    EMBPROF_NODISCARD float_t  max_val()  const noexcept { return count_ ? max_ : 0; }
    EMBPROF_NODISCARD float_t  mean()     const noexcept { return mean_; }
    EMBPROF_NODISCARD float_t  last()     const noexcept { return last_; }
    EMBPROF_NODISCARD float_t  variance() const noexcept {
        return count_ > 1 ? m2_ / static_cast<float_t>(count_ - 1) : 0;
    }
    EMBPROF_NODISCARD float_t stddev() const noexcept { return std::sqrt(variance()); }

    /// Merge another set (Chan's parallel algorithm).
    void merge(const running_stats& o) noexcept {
        if (o.count_ == 0) return;
        if (count_ == 0) { *this = o; return; }
        const uint32_t n = count_ + o.count_;
        const float_t d = o.mean_ - mean_;
        mean_ += d * static_cast<float_t>(o.count_) / static_cast<float_t>(n);
        m2_ += o.m2_ + d * d
             * (static_cast<float_t>(count_) * static_cast<float_t>(o.count_))
             / static_cast<float_t>(n);
        count_ = n;
        min_ = (std::min)(min_, o.min_);
        max_ = (std::max)(max_, o.max_);
        last_ = o.last_;
    }

private:
    uint32_t count_;
    float_t  min_, max_, mean_, m2_, last_;
};

// ============================================================================
// P² quantile estimator — 5 markers, ~80 bytes, no storage
// ============================================================================

class p2_quantile {
public:
    explicit p2_quantile(float_t p = 0.5) noexcept
        : p_(p), count_(0) {
        dn_[0] = 0; dn_[1] = p/2; dn_[2] = p; dn_[3] = (1+p)/2; dn_[4] = 1;
    }

    void push(float_t x) noexcept {
        if (count_ < 5) {
            q_[count_] = x;
            if (++count_ == 5) init_markers();
            return;
        }
        int k = -1;
        if      (x < q_[0]) { q_[0] = x; k = 0; }
        else if (x < q_[1]) { k = 0; }
        else if (x < q_[2]) { k = 1; }
        else if (x < q_[3]) { k = 2; }
        else if (x <= q_[4]){ k = 3; }
        else                 { q_[4] = x; k = 3; }

        for (int i = k+1; i < 5; ++i) ++n_[i];
        for (int i = 0; i < 5; ++i)   ns_[i] += dn_[i];

        for (int i = 1; i <= 3; ++i) {
            float_t d = ns_[i] - static_cast<float_t>(n_[i]);
            if ((d >= 1 && n_[i+1]-n_[i] > 1) || (d <= -1 && n_[i-1]-n_[i] < -1)) {
                int di = d >= 0 ? 1 : -1;
                float_t qp = parabolic(i, di);
                q_[i] = (q_[i-1] < qp && qp < q_[i+1]) ? qp : linear(i, di);
                n_[i] += di;
            }
        }
        ++count_;
    }

    EMBPROF_NODISCARD float_t get() const noexcept {
        if (count_ < 5) {
            float_t tmp[5];
            for (uint32_t i = 0; i < count_; ++i) tmp[i] = q_[i];
            for (uint32_t i = 1; i < count_; ++i) {
                float_t key = tmp[i]; uint32_t j = i;
                while (j > 0 && tmp[j-1] > key) { tmp[j] = tmp[j-1]; --j; }
                tmp[j] = key;
            }
            if (count_ == 0) return 0;
            uint32_t idx = static_cast<uint32_t>(
                static_cast<float_t>(count_-1) * p_ + 0.5f);
            return tmp[idx < count_ ? idx : count_-1];
        }
        return q_[2];
    }

    EMBPROF_NODISCARD float_t target_quantile() const noexcept { return p_; }
    EMBPROF_NODISCARD uint32_t count() const noexcept { return count_; }

private:
    void init_markers() noexcept {
        for (int i = 1; i < 5; ++i) {
            float_t key = q_[i]; int j = i;
            while (j > 0 && q_[j-1] > key) { q_[j] = q_[j-1]; --j; }
            q_[j] = key;
        }
        for (int i = 0; i < 5; ++i) n_[i] = i;
        ns_[0]=0; ns_[1]=2*p_; ns_[2]=4*p_; ns_[3]=2+2*p_; ns_[4]=4;
    }
    float_t parabolic(int i, int d) const noexcept {
        float_t fd = static_cast<float_t>(d);
        return q_[i] + fd / static_cast<float_t>(n_[i+1]-n_[i-1])
            * ((static_cast<float_t>(n_[i]-n_[i-1])+fd)
                   * (q_[i+1]-q_[i]) / static_cast<float_t>(n_[i+1]-n_[i])
               + (static_cast<float_t>(n_[i+1]-n_[i])-fd)
                   * (q_[i]-q_[i-1]) / static_cast<float_t>(n_[i]-n_[i-1]));
    }
    float_t linear(int i, int d) const noexcept {
        return q_[i] + static_cast<float_t>(d)
            * (q_[i+d]-q_[i]) / static_cast<float_t>(n_[i+d]-n_[i]);
    }

    float_t  p_;
    uint32_t count_;
    float_t  q_[5];
    int32_t  n_[5];
    float_t  ns_[5], dn_[5];
};

// ============================================================================
// Histogram — fixed buckets, linear or log-linear
// ============================================================================

enum class bucket_mode : uint8_t { linear, log_linear };

template <uint32_t N = EMBPROF_DEFAULT_HISTOGRAM_BUCKETS>
class histogram {
    static_assert(N >= 2, "Need at least 2 buckets");
public:
    histogram(float_t lo, float_t hi, bucket_mode mode = bucket_mode::log_linear) noexcept
        : mode_(mode), lo_(lo), hi_(hi), total_(0) {
        for (uint32_t i = 0; i < N+2; ++i) counts_[i] = 0;
        recompute();
    }

    void reset() noexcept {
        for (uint32_t i = 0; i < N+2; ++i) counts_[i] = 0;
        total_ = 0;
    }

    void record(float_t v) noexcept {
        ++total_;
        if (v < lo_)  { ++counts_[0]; return; }
        if (v >= hi_) { ++counts_[N+1]; return; }
        uint32_t l = 0, r = N;
        while (l < r) {
            uint32_t m = l + (r-l)/2;
            if (v >= bounds_[m+1]) l = m+1; else r = m;
        }
        ++counts_[l+1];
    }

    EMBPROF_NODISCARD uint32_t total_count() const noexcept { return total_; }
    static constexpr uint32_t bucket_count() noexcept { return N; }
    EMBPROF_NODISCARD uint32_t underflow()   const noexcept { return counts_[0]; }
    EMBPROF_NODISCARD uint32_t overflow()    const noexcept { return counts_[N+1]; }
    EMBPROF_NODISCARD uint32_t bucket(uint32_t i)    const noexcept { return i<N ? counts_[i+1] : 0; }
    EMBPROF_NODISCARD float_t  bucket_lo(uint32_t i) const noexcept { return i<N ? bounds_[i] : hi_; }
    EMBPROF_NODISCARD float_t  bucket_hi(uint32_t i) const noexcept { return i<=N ? bounds_[i+1] : hi_; }

    EMBPROF_NODISCARD float_t quantile(float_t p) const noexcept {
        if (total_ == 0) return 0;
        const float_t target = p * static_cast<float_t>(total_);
        float_t cum = static_cast<float_t>(counts_[0]);
        if (cum >= target) return lo_;
        for (uint32_t i = 0; i < N; ++i) {
            float_t prev = cum;
            cum += static_cast<float_t>(counts_[i+1]);
            if (cum >= target) {
                float_t frac = counts_[i+1] > 0
                    ? (target - prev) / static_cast<float_t>(counts_[i+1])
                    : 0.5f;
                return bounds_[i] + frac * (bounds_[i+1] - bounds_[i]);
            }
        }
        return hi_;
    }

    void merge(const histogram& o) noexcept {
        total_ += o.total_;
        for (uint32_t i = 0; i < N+2; ++i) counts_[i] += o.counts_[i];
    }

private:
    void recompute() noexcept {
        if (mode_ == bucket_mode::log_linear && lo_ > 0) {
            float_t ll = std::log(lo_), lh = std::log(hi_);
            float_t step = (lh - ll) / static_cast<float_t>(N);
            for (uint32_t i = 0; i <= N; ++i)
                bounds_[i] = std::exp(ll + static_cast<float_t>(i) * step);
            bounds_[0] = lo_; bounds_[N] = hi_;
        } else {
            float_t step = (hi_ - lo_) / static_cast<float_t>(N);
            for (uint32_t i = 0; i <= N; ++i)
                bounds_[i] = lo_ + static_cast<float_t>(i) * step;
        }
    }

    bucket_mode mode_;
    float_t lo_, hi_;
    uint32_t total_;
    float_t bounds_[N+1];
    uint32_t counts_[N+2];
};

// ============================================================================
// Profiling point — combines stats + histogram + quantiles
// ============================================================================

template <uint32_t HistBuckets = EMBPROF_DEFAULT_HISTOGRAM_BUCKETS,
          typename Clock = default_clock>
class profiling_point {
public:
    profiling_point(const char* name, float_t hist_lo, float_t hist_hi,
                    bucket_mode mode = bucket_mode::log_linear) noexcept
        : name_(name), start_(0), hist_(hist_lo, hist_hi, mode),
          p50_(0.5), p90_(0.9), p99_(0.99) {}

    void record(float_t elapsed) noexcept {
        stats_.push(elapsed);
        hist_.record(elapsed);
        p50_.push(elapsed);
        p90_.push(elapsed);
        p99_.push(elapsed);
    }

    void start() noexcept { start_ = Clock::now(); }
    void stop()  noexcept {
        tick_t end = Clock::now();
        record(static_cast<float_t>(end >= start_ ? end - start_ : start_ - end));
    }

    void reset() noexcept {
        stats_.reset(); hist_.reset();
        p50_ = p2_quantile(0.5); p90_ = p2_quantile(0.9); p99_ = p2_quantile(0.99);
    }

    EMBPROF_NODISCARD const char*                   name()       const noexcept { return name_; }
    EMBPROF_NODISCARD const running_stats&           stats()      const noexcept { return stats_; }
    EMBPROF_NODISCARD const histogram<HistBuckets>&  hist()       const noexcept { return hist_; }
    EMBPROF_NODISCARD const p2_quantile&             quantile50() const noexcept { return p50_; }
    EMBPROF_NODISCARD const p2_quantile&             quantile90() const noexcept { return p90_; }
    EMBPROF_NODISCARD const p2_quantile&             quantile99() const noexcept { return p99_; }

private:
    const char*            name_;
    tick_t                 start_;
    running_stats          stats_;
    histogram<HistBuckets> hist_;
    p2_quantile            p50_, p90_, p99_;
};

// ============================================================================
// RAII scoped timer
// ============================================================================

template <uint32_t HistBuckets = EMBPROF_DEFAULT_HISTOGRAM_BUCKETS,
          typename Clock = default_clock>
class scoped_timer {
public:
    explicit scoped_timer(profiling_point<HistBuckets, Clock>& pp) noexcept
        : pp_(pp), start_(Clock::now()) {}
    ~scoped_timer() noexcept {
        tick_t end = Clock::now();
        pp_.record(static_cast<float_t>(end >= start_ ? end - start_ : start_ - end));
    }
    scoped_timer(const scoped_timer&)            = delete;
    scoped_timer& operator=(const scoped_timer&) = delete;

private:
    profiling_point<HistBuckets, Clock>& pp_;
    tick_t start_;
};

} // namespace embprof

#endif // EMBPROF_LITE_HPP
