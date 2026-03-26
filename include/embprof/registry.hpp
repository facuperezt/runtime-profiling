#ifndef EMBPROF_REGISTRY_HPP
#define EMBPROF_REGISTRY_HPP

/// @file registry.hpp
/// @brief Global registry of profiling points for enumeration / reporting.
///
/// Stores up to EMBPROF_MAX_PROFILING_POINTS type-erased pointers.
/// Zero dynamic allocation.  ISR-safe (no locks — register only at init time).

#include "config.hpp"
#include "profiling_point.hpp"

#include <cstdint>

namespace embprof {

// ---------------------------------------------------------------------------
// Type-erased wrapper so the registry can hold heterogeneous profiling_points.
// ---------------------------------------------------------------------------

/// Minimal virtual interface for iteration / reporting.
class profiling_point_base {
public:
    virtual ~profiling_point_base() = default;
    virtual const char*           name()    const noexcept = 0;
    virtual const running_stats&  stats()   const noexcept = 0;
    virtual float_t               p50()     const noexcept = 0;
    virtual float_t               p90()     const noexcept = 0;
    virtual float_t               p99()     const noexcept = 0;
    virtual void                  reset()         noexcept = 0;
};

/// Adapter that wraps a concrete profiling_point<N,C>.
template <uint32_t N, typename C>
class profiling_point_adapter : public profiling_point_base {
public:
    explicit profiling_point_adapter(profiling_point<N, C>& pp) noexcept : pp_(pp) {}

    const char*          name()  const noexcept override { return pp_.name(); }
    const running_stats& stats() const noexcept override { return pp_.stats(); }
    float_t              p50()   const noexcept override { return pp_.quantile50().get(); }
    float_t              p90()   const noexcept override { return pp_.quantile90().get(); }
    float_t              p99()   const noexcept override { return pp_.quantile99().get(); }
    void                 reset()       noexcept override { pp_.reset(); }

private:
    profiling_point<N, C>& pp_;
};

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

#if EMBPROF_MAX_PROFILING_POINTS > 0

class registry {
public:
    static registry& instance() noexcept {
        static registry r;
        return r;
    }

    /// Register a profiling point.  Returns false if full.
    bool add(profiling_point_base* pp) noexcept {
        if (size_ >= EMBPROF_MAX_PROFILING_POINTS) return false;
        entries_[size_++] = pp;
        return true;
    }

    EMBPROF_NODISCARD uint32_t              size()  const noexcept { return size_; }
    EMBPROF_NODISCARD profiling_point_base* get(uint32_t i) const noexcept {
        return (i < size_) ? entries_[i] : nullptr;
    }

    /// Call fn(profiling_point_base&) for every registered point.
    template <typename Fn>
    void for_each(Fn&& fn) const {
        for (uint32_t i = 0; i < size_; ++i) {
            fn(*entries_[i]);
        }
    }

    void reset_all() noexcept {
        for (uint32_t i = 0; i < size_; ++i) {
            entries_[i]->reset();
        }
    }

    void clear() noexcept {
        size_ = 0;
    }

private:
    registry() noexcept : size_(0) {
        for (uint32_t i = 0; i < EMBPROF_MAX_PROFILING_POINTS; ++i) {
            entries_[i] = nullptr;
        }
    }

    uint32_t              size_;
    profiling_point_base* entries_[EMBPROF_MAX_PROFILING_POINTS];
};

/// RAII helper: registers a profiling_point in the global registry at
/// construction time.
template <uint32_t N = EMBPROF_DEFAULT_HISTOGRAM_BUCKETS,
          typename C = default_clock>
class registered_point {
public:
    registered_point(profiling_point<N, C>& pp) noexcept
        : adapter_(pp)
    {
        registry::instance().add(&adapter_);
    }

private:
    profiling_point_adapter<N, C> adapter_;
};

#endif // EMBPROF_MAX_PROFILING_POINTS > 0

} // namespace embprof

#endif // EMBPROF_REGISTRY_HPP
