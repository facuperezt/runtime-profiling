/// @file basic_usage.cpp
/// @brief Demonstrates core embprof features.

#include <embprof/embprof.hpp>

#include <cstdio>
#include <cstdlib>

// On desktop we use chrono_clock (the default).
// On your MCU, define EMBPROF_CLOCK_DWT or EMBPROF_CLOCK_SYSTICK
// or provide your own via EMBPROF_CLOCK_USER.

// Create a profiling point: name, histogram lower bound, histogram upper bound.
static embprof::profiling_point<20> motor_control_loop(
    "motor_control_loop",
    100.0,     // expected minimum ticks
    100000.0,  // expected maximum ticks
    embprof::bucket_mode::log_linear
);

// Register it in the global registry for easy enumeration.
static embprof::registered_point<20> reg_motor(motor_control_loop);

// A second point using the convenience macro.
EMBPROF_POINT(can_handler, 50.0, 50000.0, embprof::bucket_mode::log_linear);

/// Simulate some work with variable duration.
static void simulate_work() {
    volatile int x = 0;
    int iters = 100 + (std::rand() % 900);
    for (int i = 0; i < iters; ++i) {
        x += i;
    }
    (void)x;
}

int main() {
    std::printf("embprof basic usage example\n");
    std::printf("==========================\n\n");

    // ----- Method 1: RAII scoped timer -----
    for (int i = 0; i < 500; ++i) {
        EMBPROF_SCOPE(motor_control_loop);
        simulate_work();
    }

    // ----- Method 2: Manual start/stop -----
    for (int i = 0; i < 200; ++i) {
        EMBPROF_START(can_handler);
        simulate_work();
        EMBPROF_STOP(can_handler);
    }

    // ----- Print summary via registry -----
    std::printf("--- Profiling Summary ---\n\n");

    embprof::registry::instance().for_each([](embprof::profiling_point_base& pp) {
        const auto& s = pp.stats();
        std::printf("  [%s]\n", pp.name());
        std::printf("    count : %u\n", s.count());
        std::printf("    mean  : %.1f\n", static_cast<double>(s.mean()));
        std::printf("    min   : %.1f\n", static_cast<double>(s.min_val()));
        std::printf("    max   : %.1f\n", static_cast<double>(s.max_val()));
        std::printf("    stddev: %.1f\n", static_cast<double>(s.stddev()));
        std::printf("    p50   : %.1f\n", static_cast<double>(pp.p50()));
        std::printf("    p90   : %.1f\n", static_cast<double>(pp.p90()));
        std::printf("    p99   : %.1f\n", static_cast<double>(pp.p99()));
        std::printf("\n");
    });

    // ----- Histogram dump -----
    std::printf("--- motor_control_loop Histogram ---\n");
    const auto& h = motor_control_loop.hist();
    std::printf("  underflow: %u\n", h.underflow());
    for (uint32_t i = 0; i < h.bucket_count(); ++i) {
        std::printf("  [%8.1f, %8.1f): %u\n",
            static_cast<double>(h.bucket_lo(i)),
            static_cast<double>(h.bucket_hi(i)),
            h.bucket(i));
    }
    std::printf("  overflow : %u\n", h.overflow());

    // ----- Serialization demo -----
    constexpr auto buf_size = embprof::serialized_size<20, embprof::default_clock>();
    uint8_t buf[buf_size];
    uint32_t written = embprof::serialize(motor_control_loop, buf, buf_size);
    std::printf("\n--- Serialized %u bytes ---\n", written);

    // Restore into a fresh point
    embprof::profiling_point<20> restored("restored", 100, 100000,
                                          embprof::bucket_mode::log_linear);
    if (embprof::deserialize(restored, buf, written)) {
        std::printf("  Restored count: %u, mean: %.1f\n",
            restored.stats().count(),
            static_cast<double>(restored.stats().mean()));
    }

    return 0;
}
