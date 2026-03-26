#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <embprof/profiling_point.hpp>

#include <thread>
#include <chrono>

using namespace embprof;
using Catch::Matchers::WithinAbs;

// ---------------------------------------------------------------------------
// Fake clock for deterministic scoped_timer tests
// ---------------------------------------------------------------------------
struct scoped_fake_clock {
    static tick_t value;
    static tick_t now() noexcept { return value; }
};
tick_t scoped_fake_clock::value = 0;

using fake_point = profiling_point<10, scoped_fake_clock>;
using fake_timer = scoped_timer<10, scoped_fake_clock>;

TEST_CASE("scoped_timer: measures block with fake clock", "[scoped_timer]") {
    fake_point pp("scoped_fake", 0, 10000);

    {
        scoped_fake_clock::value = 1000;
        fake_timer t(pp);
        scoped_fake_clock::value = 1500; // 500 ticks elapsed
    } // destructor records

    CHECK(pp.stats().count() == 1);
    CHECK_THAT(pp.stats().mean(), WithinAbs(500.0, 1e-10));
}

TEST_CASE("scoped_timer: multiple scoped blocks", "[scoped_timer]") {
    fake_point pp("scoped_multi", 0, 10000);

    // Block 1: elapsed = 100
    {
        scoped_fake_clock::value = 0;
        fake_timer t(pp);
        scoped_fake_clock::value = 100;
    }
    // Block 2: elapsed = 300
    {
        scoped_fake_clock::value = 200;
        fake_timer t(pp);
        scoped_fake_clock::value = 500;
    }
    // Block 3: elapsed = 200
    {
        scoped_fake_clock::value = 600;
        fake_timer t(pp);
        scoped_fake_clock::value = 800;
    }

    CHECK(pp.stats().count() == 3);
    // mean of {100, 300, 200} = 200
    CHECK_THAT(pp.stats().mean(), WithinAbs(200.0, 1e-10));
    CHECK_THAT(pp.stats().min_val(), WithinAbs(100.0, 1e-10));
    CHECK_THAT(pp.stats().max_val(), WithinAbs(300.0, 1e-10));
}

TEST_CASE("scoped_timer: zero elapsed", "[scoped_timer]") {
    fake_point pp("scoped_zero", 0, 1000);

    {
        scoped_fake_clock::value = 42;
        fake_timer t(pp);
        // Don't change the clock
    }

    CHECK(pp.stats().count() == 1);
    CHECK_THAT(pp.stats().mean(), WithinAbs(0.0, 1e-10));
}

TEST_CASE("scoped_timer: feeds histogram and quantiles", "[scoped_timer]") {
    fake_point pp("scoped_histo", 0, 10000);

    for (int i = 1; i <= 20; ++i) {
        scoped_fake_clock::value = 0;
        fake_timer t(pp);
        scoped_fake_clock::value = static_cast<tick_t>(i * 100);
    }

    CHECK(pp.stats().count() == 20);
    CHECK(pp.hist().total_count() == 20);
    CHECK(pp.quantile50().count() == 20);
}

// ---------------------------------------------------------------------------
// Real-time test with chrono_clock (sanity check)
// ---------------------------------------------------------------------------
TEST_CASE("scoped_timer: real chrono_clock measures non-trivial time", "[scoped_timer][chrono]") {
    profiling_point<10, chrono_clock> pp("scoped_chrono", 0, 1e9);

    {
        scoped_timer<10, chrono_clock> t(pp);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    CHECK(pp.stats().count() == 1);
    // Should have measured at least ~5ms in nanoseconds (allowing scheduler variance)
    CHECK(pp.stats().mean() > 1e6); // > 1ms
}

TEST_CASE("scoped_timer: chrono_clock multiple samples", "[scoped_timer][chrono]") {
    profiling_point<10, chrono_clock> pp("multi_chrono", 0, 1e9);

    for (int i = 0; i < 5; ++i) {
        scoped_timer<10, chrono_clock> t(pp);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    CHECK(pp.stats().count() == 5);
    CHECK(pp.stats().mean() > 5e5);  // > 0.5ms average
    CHECK(pp.stats().stddev() >= 0); // just ensure it doesn't NaN
}
