#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <embprof/profiling_point.hpp>

using namespace embprof;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// A fake clock for deterministic testing.
struct fake_clock {
    static tick_t value;
    static tick_t now() noexcept { return value; }
};
tick_t fake_clock::value = 0;

using test_point = profiling_point<10, fake_clock>;

TEST_CASE("profiling_point: basic record", "[profiling_point]") {
    test_point pp("test_func", 0.0, 1000.0);

    pp.record(100.0);
    pp.record(200.0);
    pp.record(300.0);

    CHECK(pp.stats().count() == 3);
    CHECK_THAT(pp.stats().mean(), WithinAbs(200.0, 1e-10));
    CHECK_THAT(pp.stats().min_val(), WithinAbs(100.0, 1e-10));
    CHECK_THAT(pp.stats().max_val(), WithinAbs(300.0, 1e-10));
    CHECK(pp.hist().total_count() == 3);
}

TEST_CASE("profiling_point: name accessor", "[profiling_point]") {
    test_point pp("my_function", 0.0, 1000.0);
    CHECK(std::string(pp.name()) == "my_function");
}

TEST_CASE("profiling_point: manual start/stop with fake clock", "[profiling_point]") {
    test_point pp("manual_test", 0.0, 1000.0);

    fake_clock::value = 100;
    pp.start();
    fake_clock::value = 350;
    pp.stop();

    CHECK(pp.stats().count() == 1);
    CHECK_THAT(pp.stats().mean(), WithinAbs(250.0, 1e-10));
}

TEST_CASE("profiling_point: multiple start/stop cycles", "[profiling_point]") {
    test_point pp("multi_cycle", 0.0, 5000.0);

    fake_clock::value = 0;   pp.start();
    fake_clock::value = 100; pp.stop();  // elapsed = 100

    fake_clock::value = 200; pp.start();
    fake_clock::value = 500; pp.stop();  // elapsed = 300

    fake_clock::value = 600; pp.start();
    fake_clock::value = 800; pp.stop();  // elapsed = 200

    CHECK(pp.stats().count() == 3);
    CHECK_THAT(pp.stats().mean(), WithinAbs(200.0, 1e-10));
    CHECK_THAT(pp.stats().min_val(), WithinAbs(100.0, 1e-10));
    CHECK_THAT(pp.stats().max_val(), WithinAbs(300.0, 1e-10));
}

TEST_CASE("profiling_point: quantile estimators are fed", "[profiling_point]") {
    test_point pp("quantile_test", 0.0, 1000.0);

    for (int i = 0; i < 100; ++i) {
        pp.record(static_cast<double>(i));
    }

    CHECK(pp.quantile50().count() == 100);
    CHECK(pp.quantile90().count() == 100);
    CHECK(pp.quantile99().count() == 100);

    // p50 of 0..99 ≈ 49.5
    CHECK_THAT(pp.quantile50().get(), WithinAbs(49.5, 10.0));
}

TEST_CASE("profiling_point: reset clears everything", "[profiling_point]") {
    test_point pp("reset_test", 0.0, 1000.0);
    pp.record(50); pp.record(100); pp.record(150);

    pp.reset();

    CHECK(pp.stats().count() == 0);
    CHECK(pp.hist().total_count() == 0);
    CHECK(pp.quantile50().count() == 0);
    CHECK(pp.quantile90().count() == 0);
    CHECK(pp.quantile99().count() == 0);
}

TEST_CASE("profiling_point: snapshot and restore roundtrip", "[profiling_point]") {
    test_point pp("snap_test", 0.0, 1000.0);
    for (int i = 0; i < 50; ++i) {
        pp.record(static_cast<double>(i * 10));
    }

    auto snap = pp.snapshot();

    test_point pp2("snap_test_2", 0.0, 1000.0);
    pp2.restore(snap);

    CHECK(pp2.stats().count() == pp.stats().count());
    CHECK_THAT(pp2.stats().mean(), WithinAbs(pp.stats().mean(), 1e-10));
    CHECK_THAT(pp2.stats().variance(), WithinAbs(pp.stats().variance(), 1e-6));
    CHECK(pp2.hist().total_count() == pp.hist().total_count());
    CHECK_THAT(pp2.quantile50().get(), WithinAbs(pp.quantile50().get(), 1e-10));
}

TEST_CASE("profiling_point: histogram mode selection", "[profiling_point]") {
    // Linear mode
    profiling_point<10, fake_clock> pp_lin("linear", 0, 100, bucket_mode::linear);
    pp_lin.record(50);
    CHECK(pp_lin.hist().bucket(5) == 1); // [50, 60)

    // Log-linear mode
    profiling_point<10, fake_clock> pp_log("loglin", 1, 10000, bucket_mode::log_linear);
    pp_log.record(50);
    CHECK(pp_log.hist().total_count() == 1);
}

TEST_CASE("profiling_point: data continues after restore", "[profiling_point]") {
    test_point pp("continue", 0.0, 1000.0);
    for (int i = 0; i < 20; ++i) pp.record(static_cast<double>(i));

    auto snap = pp.snapshot();

    test_point pp2("continue_2", 0.0, 1000.0);
    pp2.restore(snap);

    // Push more data
    for (int i = 20; i < 40; ++i) pp2.record(static_cast<double>(i));

    CHECK(pp2.stats().count() == 40);
    CHECK_THAT(pp2.stats().mean(), WithinAbs(19.5, 1e-6));
}
