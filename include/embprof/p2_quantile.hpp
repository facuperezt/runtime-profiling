#ifndef EMBPROF_P2_QUANTILE_HPP
#define EMBPROF_P2_QUANTILE_HPP

/// @file p2_quantile.hpp
/// @brief P² algorithm for online quantile estimation without storing observations.
///
/// Uses only 5 markers — fixed ~80 bytes regardless of observation count.
/// Reference: Jain & Chlamtac, "The P-Square Algorithm for Dynamic Calculation
/// of Percentiles and Histograms Without Storing Observations", CACM 1985.

#include "config.hpp"
#include "clock.hpp"

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace embprof {

class p2_quantile {
public:
    /// @param p  The quantile to track, e.g. 0.5 for median, 0.99 for p99.
    explicit p2_quantile(float_t p = 0.5) noexcept
        : p_(p), count_(0)
    {
        // Desired marker positions (increments per observation)
        dn_[0] = 0;
        dn_[1] = p / 2;
        dn_[2] = p;
        dn_[3] = (1 + p) / 2;
        dn_[4] = 1;
    }

    /// Feed one observation.
    void push(float_t x) noexcept {
        if (count_ < 5) {
            q_[count_] = x;
            ++count_;
            if (count_ == 5) {
                init_markers();
            }
            return;
        }

        // Find cell k such that q_[k] <= x < q_[k+1]
        int k = -1;
        if (x < q_[0]) {
            q_[0] = x;
            k = 0;
        } else if (x < q_[1]) {
            k = 0;
        } else if (x < q_[2]) {
            k = 1;
        } else if (x < q_[3]) {
            k = 2;
        } else if (x <= q_[4]) {
            k = 3;
        } else {
            q_[4] = x;
            k = 3;
        }

        // Increment positions of markers k+1 .. 4
        for (int i = k + 1; i < 5; ++i) {
            ++n_[i];
        }

        // Update desired positions
        for (int i = 0; i < 5; ++i) {
            ns_[i] += dn_[i];
        }

        // Adjust marker heights 1, 2, 3
        for (int i = 1; i <= 3; ++i) {
            float_t d = ns_[i] - static_cast<float_t>(n_[i]);
            if ((d >=  1 && (n_[i + 1] - n_[i]) > 1) ||
                (d <= -1 && (n_[i - 1] - n_[i]) < -1)) {
                int di = (d >= 0) ? 1 : -1;
                float_t qp = parabolic(i, di);
                if (q_[i - 1] < qp && qp < q_[i + 1]) {
                    q_[i] = qp;
                } else {
                    q_[i] = linear(i, di);
                }
                n_[i] += di;
            }
        }

        ++count_;
    }

    /// Get the estimated quantile value.
    EMBPROF_NODISCARD float_t get() const noexcept {
        if (count_ < 5) {
            // Not enough data — sort the few we have and interpolate.
            float_t tmp[5];
            for (uint32_t i = 0; i < count_; ++i) tmp[i] = q_[i];
            // Insertion sort (at most 4 elements)
            for (uint32_t i = 1; i < count_; ++i) {
                float_t key = tmp[i];
                uint32_t j = i;
                while (j > 0 && tmp[j - 1] > key) {
                    tmp[j] = tmp[j - 1];
                    --j;
                }
                tmp[j] = key;
            }
            if (count_ == 0) return 0;
            uint32_t idx = static_cast<uint32_t>(
                static_cast<float_t>(count_ - 1) * p_ + static_cast<float_t>(0.5));
            if (idx >= count_) idx = count_ - 1;
            return tmp[idx];
        }
        return q_[2]; // The middle marker tracks the p-quantile.
    }

    EMBPROF_NODISCARD float_t target_quantile() const noexcept { return p_; }
    EMBPROF_NODISCARD uint32_t count() const noexcept { return count_; }

    // -- Serialization -------------------------------------------------------
    struct state_t {
        float_t  p;
        uint32_t count;
        float_t  q[5];
        int32_t  n[5];
        float_t  ns[5];
        float_t  dn[5];
    };

    EMBPROF_NODISCARD state_t snapshot() const noexcept {
        state_t s;
        s.p     = p_;
        s.count = count_;
        for (int i = 0; i < 5; ++i) {
            s.q[i]  = q_[i];
            s.n[i]  = n_[i];
            s.ns[i] = ns_[i];
            s.dn[i] = dn_[i];
        }
        return s;
    }

    void restore(const state_t& s) noexcept {
        p_     = s.p;
        count_ = s.count;
        for (int i = 0; i < 5; ++i) {
            q_[i]  = s.q[i];
            n_[i]  = s.n[i];
            ns_[i] = s.ns[i];
            dn_[i] = s.dn[i];
        }
    }

private:
    void init_markers() noexcept {
        // Sort initial 5 values — insertion sort
        for (int i = 1; i < 5; ++i) {
            float_t key = q_[i];
            int j = i;
            while (j > 0 && q_[j - 1] > key) {
                q_[j] = q_[j - 1];
                --j;
            }
            q_[j] = key;
        }

        for (int i = 0; i < 5; ++i) {
            n_[i] = i;
        }

        ns_[0] = 0;
        ns_[1] = 2 * p_;
        ns_[2] = 4 * p_;
        ns_[3] = 2 + 2 * p_;
        ns_[4] = 4;
    }

    float_t parabolic(int i, int d) const noexcept {
        float_t fd = static_cast<float_t>(d);
        return q_[i] + fd / static_cast<float_t>(n_[i + 1] - n_[i - 1])
            * ((static_cast<float_t>(n_[i] - n_[i - 1]) + fd)
                   * (q_[i + 1] - q_[i]) / static_cast<float_t>(n_[i + 1] - n_[i])
               + (static_cast<float_t>(n_[i + 1] - n_[i]) - fd)
                   * (q_[i] - q_[i - 1]) / static_cast<float_t>(n_[i] - n_[i - 1]));
    }

    float_t linear(int i, int d) const noexcept {
        return q_[i] + static_cast<float_t>(d)
            * (q_[i + d] - q_[i]) / static_cast<float_t>(n_[i + d] - n_[i]);
    }

    float_t  p_;
    uint32_t count_;
    float_t  q_[5];     // Marker heights
    int32_t  n_[5];     // Marker positions
    float_t  ns_[5];    // Desired marker positions
    float_t  dn_[5];    // Desired position increments
};

} // namespace embprof

#endif // EMBPROF_P2_QUANTILE_HPP
