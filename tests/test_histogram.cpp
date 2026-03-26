#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <embprof/histogram.hpp>

using namespace embprof;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE("histogram: linear bucket boundaries", "[histogram]") {
    histogram<10> h(0.0, 100.0);

    CHECK(h.bucket_count() == 10);
    CHECK_THAT(h.bucket_lo(0), WithinAbs(0.0, 1e-10));
    CHECK_THAT(h.bucket_hi(0), WithinAbs(10.0, 1e-10));
    CHECK_THAT(h.bucket_lo(9), WithinAbs(90.0, 1e-10));
    CHECK_THAT(h.bucket_hi(9), WithinAbs(100.0, 1e-10));
}

TEST_CASE("histogram: record into correct linear bucket", "[histogram]") {
    histogram<10> h(0.0, 100.0);

    h.record(5.0);   // bucket 0 [0, 10)
    h.record(15.0);  // bucket 1 [10, 20)
    h.record(95.0);  // bucket 9 [90, 100)

    CHECK(h.bucket(0) == 1);
    CHECK(h.bucket(1) == 1);
    CHECK(h.bucket(9) == 1);
    CHECK(h.total_count() == 3);
    CHECK(h.underflow() == 0);
    CHECK(h.overflow() == 0);
}

TEST_CASE("histogram: underflow and overflow", "[histogram]") {
    histogram<5> h(10.0, 50.0);

    h.record(5.0);    // underflow
    h.record(9.99);   // underflow
    h.record(50.0);   // overflow (>= hi)
    h.record(100.0);  // overflow

    CHECK(h.underflow() == 2);
    CHECK(h.overflow() == 2);
    CHECK(h.total_count() == 4);
}

TEST_CASE("histogram: boundary values", "[histogram]") {
    histogram<4> h(0.0, 100.0);

    // Value exactly at lo goes to bucket 0
    h.record(0.0);
    CHECK(h.bucket(0) == 1);

    // Value exactly at a boundary goes to the upper bucket
    h.record(25.0); // boundary between bucket 0 and 1 → bucket 1
    CHECK(h.bucket(1) == 1);

    // Value exactly at hi goes to overflow
    h.record(100.0);
    CHECK(h.overflow() == 1);
}

TEST_CASE("histogram: log-linear bucket spacing", "[histogram]") {
    histogram<4> h(1.0, 10000.0, bucket_mode::log_linear);

    // Buckets should be log-spaced:
    // boundaries ≈ [1, 10, 100, 1000, 10000]
    CHECK_THAT(h.bucket_lo(0), WithinAbs(1.0, 1e-6));
    CHECK(h.bucket_hi(0) > 1.0);
    CHECK(h.bucket_hi(0) < 100.0);

    // Record values in different orders of magnitude
    h.record(5.0);      // should land in first bucket
    h.record(50.0);     // somewhere in the middle
    h.record(500.0);    // mid-high
    h.record(5000.0);   // high

    CHECK(h.total_count() == 4);
    CHECK(h.underflow() == 0);
    CHECK(h.overflow() == 0);
}

TEST_CASE("histogram: reset", "[histogram]") {
    histogram<5> h(0.0, 50.0);
    h.record(10); h.record(20); h.record(30);
    CHECK(h.total_count() == 3);

    h.reset();
    CHECK(h.total_count() == 0);
    CHECK(h.underflow() == 0);
    CHECK(h.overflow() == 0);
    for (uint32_t i = 0; i < 5; ++i) {
        CHECK(h.bucket(i) == 0);
    }
}

TEST_CASE("histogram: quantile from linear histogram", "[histogram]") {
    histogram<10> h(0.0, 100.0);

    // Fill uniformly: 10 values per bucket
    for (int i = 0; i < 100; ++i) {
        h.record(static_cast<double>(i) + 0.5);
    }

    // Median should be near 50
    CHECK_THAT(h.quantile(0.5), WithinAbs(50.0, 5.0));
    // p90 should be near 90
    CHECK_THAT(h.quantile(0.9), WithinAbs(90.0, 5.0));
    // p10 should be near 10
    CHECK_THAT(h.quantile(0.1), WithinAbs(10.0, 5.0));
}

TEST_CASE("histogram: quantile with empty histogram", "[histogram]") {
    histogram<5> h(0.0, 50.0);
    CHECK(h.quantile(0.5) == 0);
}

TEST_CASE("histogram: snapshot and restore", "[histogram]") {
    histogram<8> h(0.0, 80.0);
    h.record(5); h.record(15); h.record(75);

    auto snap = h.snapshot();

    histogram<8> h2(0.0, 80.0);
    h2.restore(snap);

    CHECK(h2.total_count() == h.total_count());
    CHECK(h2.underflow() == h.underflow());
    CHECK(h2.overflow() == h.overflow());
    for (uint32_t i = 0; i < 8; ++i) {
        CHECK(h2.bucket(i) == h.bucket(i));
    }
}

TEST_CASE("histogram: merge two histograms", "[histogram]") {
    histogram<5> a(0.0, 50.0);
    histogram<5> b(0.0, 50.0);

    a.record(5); a.record(15);
    b.record(25); b.record(35); b.record(45);

    a.merge(b);
    CHECK(a.total_count() == 5);
    CHECK(a.bucket(0) == 1); // 5
    CHECK(a.bucket(1) == 1); // 15
    CHECK(a.bucket(2) == 1); // 25
    CHECK(a.bucket(3) == 1); // 35
    CHECK(a.bucket(4) == 1); // 45
}

TEST_CASE("histogram: many values concentrate in one bucket", "[histogram]") {
    histogram<10> h(0.0, 100.0);
    for (int i = 0; i < 1000; ++i) {
        h.record(55.0); // all in bucket 5 [50, 60)
    }
    CHECK(h.bucket(5) == 1000);
    CHECK(h.total_count() == 1000);
    for (uint32_t i = 0; i < 10; ++i) {
        if (i != 5) CHECK(h.bucket(i) == 0);
    }
}

TEST_CASE("histogram: out-of-range bucket index returns 0", "[histogram]") {
    histogram<5> h(0.0, 50.0);
    h.record(10);
    CHECK(h.bucket(100) == 0);
}
