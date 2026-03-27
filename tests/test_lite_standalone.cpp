// Standalone compilation test for embprof_lite.hpp — no other headers needed.
// This file intentionally does NOT include anything from include/embprof/.

#include "../single_include/embprof_lite.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <thread>
#include <chrono>

using namespace embprof;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// -- Fake clock for deterministic tests --
struct lite_fake_clock {
    static tick_t value;
    static tick_t now() noexcept { return value; }
};
tick_t lite_fake_clock::value = 0;

using lite_point = profiling_point<10, lite_fake_clock>;
using lite_timer = scoped_timer<10, lite_fake_clock>;

// ========================================================================
// running_stats
// ========================================================================

TEST_CASE("lite: running_stats basics", "[lite]") {
    running_stats rs;
    CHECK(rs.count() == 0);
    CHECK(rs.mean() == 0);

    double vals[] = {2, 4, 4, 4, 5, 5, 7, 9};
    for (double v : vals) rs.push(v);

    CHECK(rs.count() == 8);
    CHECK_THAT(rs.mean(), WithinAbs(5.0, 1e-10));
    CHECK_THAT(rs.min_val(), WithinAbs(2.0, 1e-10));
    CHECK_THAT(rs.max_val(), WithinAbs(9.0, 1e-10));
    CHECK_THAT(rs.variance(), WithinRel(4.571428571, 1e-6));
}

TEST_CASE("lite: running_stats merge", "[lite]") {
    running_stats a, b;
    a.push(1); a.push(2); a.push(3);
    b.push(4); b.push(5); b.push(6);
    a.merge(b);
    CHECK(a.count() == 6);
    CHECK_THAT(a.mean(), WithinAbs(3.5, 1e-10));
}

TEST_CASE("lite: running_stats numerical stability", "[lite]") {
    running_stats rs;
    const double base = 1e9;
    for (int i = 1; i <= 5; ++i) rs.push(base + i);
    CHECK_THAT(rs.mean(), WithinAbs(base + 3.0, 1e-4));
    CHECK_THAT(rs.variance(), WithinRel(2.5, 1e-6));
}

// ========================================================================
// p2_quantile
// ========================================================================

TEST_CASE("lite: p2_quantile median", "[lite]") {
    p2_quantile pq(0.5);
    for (int i = 0; i < 1000; ++i) pq.push(static_cast<double>(i));
    CHECK_THAT(pq.get(), WithinAbs(499.5, 25.0));
}

TEST_CASE("lite: p2_quantile p99", "[lite]") {
    p2_quantile pq(0.99);
    for (int i = 0; i < 10000; ++i) pq.push(static_cast<double>(i));
    CHECK_THAT(pq.get(), WithinAbs(9899.0, 200.0));
}

TEST_CASE("lite: p2_quantile fewer than 5", "[lite]") {
    p2_quantile pq(0.5);
    pq.push(10); pq.push(20); pq.push(30);
    CHECK_THAT(pq.get(), WithinAbs(20.0, 1e-10));
}

// ========================================================================
// histogram
// ========================================================================

TEST_CASE("lite: histogram linear", "[lite]") {
    histogram<10> h(0.0, 100.0, bucket_mode::linear);
    h.record(5.0);   // bucket 0
    h.record(95.0);  // bucket 9
    h.record(-1.0);  // underflow
    h.record(100.0); // overflow

    CHECK(h.bucket(0) == 1);
    CHECK(h.bucket(9) == 1);
    CHECK(h.underflow() == 1);
    CHECK(h.overflow() == 1);
    CHECK(h.total_count() == 4);
}

TEST_CASE("lite: histogram log-linear", "[lite]") {
    histogram<4> h(1.0, 10000.0, bucket_mode::log_linear);
    h.record(5.0);
    h.record(500.0);
    h.record(5000.0);
    CHECK(h.total_count() == 3);
    CHECK(h.underflow() == 0);
    CHECK(h.overflow() == 0);
}

TEST_CASE("lite: histogram quantile", "[lite]") {
    histogram<10> h(0.0, 100.0, bucket_mode::linear);
    for (int i = 0; i < 100; ++i) h.record(static_cast<double>(i) + 0.5);
    CHECK_THAT(h.quantile(0.5), WithinAbs(50.0, 5.0));
    CHECK_THAT(h.quantile(0.9), WithinAbs(90.0, 5.0));
}

// ========================================================================
// profiling_point + scoped_timer with fake clock
// ========================================================================

TEST_CASE("lite: profiling_point record", "[lite]") {
    lite_point pp("test", 0, 1000, bucket_mode::linear);
    pp.record(100); pp.record(200); pp.record(300);
    CHECK(pp.stats().count() == 3);
    CHECK_THAT(pp.stats().mean(), WithinAbs(200.0, 1e-10));
}

TEST_CASE("lite: manual start/stop", "[lite]") {
    lite_point pp("manual", 0, 10000);
    lite_fake_clock::value = 100;
    pp.start();
    lite_fake_clock::value = 450;
    pp.stop();
    CHECK(pp.stats().count() == 1);
    CHECK_THAT(pp.stats().mean(), WithinAbs(350.0, 1e-10));
}

TEST_CASE("lite: scoped_timer", "[lite]") {
    lite_point pp("scoped", 0, 10000);
    {
        lite_fake_clock::value = 1000;
        lite_timer t(pp);
        lite_fake_clock::value = 1500;
    }
    CHECK(pp.stats().count() == 1);
    CHECK_THAT(pp.stats().mean(), WithinAbs(500.0, 1e-10));
}

TEST_CASE("lite: scoped_timer feeds quantiles and histogram", "[lite]") {
    lite_point pp("all_fed", 0, 10000);
    for (int i = 1; i <= 20; ++i) {
        lite_fake_clock::value = 0;
        lite_timer t(pp);
        lite_fake_clock::value = static_cast<tick_t>(i * 100);
    }
    CHECK(pp.stats().count() == 20);
    CHECK(pp.hist().total_count() == 20);
    CHECK(pp.quantile50().count() == 20);
    CHECK(pp.quantile99().count() == 20);
}

TEST_CASE("lite: reset clears everything", "[lite]") {
    lite_point pp("resettable", 0, 1000);
    pp.record(50); pp.record(100);
    pp.reset();
    CHECK(pp.stats().count() == 0);
    CHECK(pp.hist().total_count() == 0);
    CHECK(pp.quantile50().count() == 0);
}

// ========================================================================
// Real chrono_clock sanity check
// ========================================================================

TEST_CASE("lite: chrono_clock real timing", "[lite][chrono]") {
    profiling_point<10, chrono_clock> pp("chrono_test", 0, 1e9);
    {
        scoped_timer<10, chrono_clock> t(pp);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(pp.stats().count() == 1);
    CHECK(pp.stats().mean() > 1e6); // > 1ms in nanoseconds
}
