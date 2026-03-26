#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <embprof/p2_quantile.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

using namespace embprof;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE("p2_quantile: empty state", "[p2_quantile]") {
    p2_quantile pq(0.5);
    CHECK(pq.count() == 0);
    CHECK(pq.get() == 0);
    CHECK_THAT(pq.target_quantile(), WithinAbs(0.5, 1e-10));
}

TEST_CASE("p2_quantile: fewer than 5 observations", "[p2_quantile]") {
    p2_quantile pq(0.5);
    pq.push(10);
    CHECK(pq.count() == 1);
    CHECK_THAT(pq.get(), WithinAbs(10.0, 1e-10));

    pq.push(20);
    CHECK(pq.count() == 2);
    // Median of {10, 20} — should pick one of them (nearest index)
    CHECK((pq.get() >= 10.0 && pq.get() <= 20.0));

    pq.push(30);
    CHECK(pq.count() == 3);
    CHECK_THAT(pq.get(), WithinAbs(20.0, 1e-10)); // median of {10, 20, 30}
}

TEST_CASE("p2_quantile: exact with 5 values", "[p2_quantile]") {
    p2_quantile pq(0.5);
    pq.push(1); pq.push(2); pq.push(3); pq.push(4); pq.push(5);

    CHECK(pq.count() == 5);
    // After init with exactly 5, markers are sorted [1,2,3,4,5]
    // Median marker (index 2) = 3
    CHECK_THAT(pq.get(), WithinAbs(3.0, 1e-6));
}

TEST_CASE("p2_quantile: median of uniform distribution", "[p2_quantile]") {
    p2_quantile pq(0.5);

    // Feed 0..999 — true median is ~499.5
    for (int i = 0; i < 1000; ++i) {
        pq.push(static_cast<double>(i));
    }

    CHECK(pq.count() == 1000);
    // P² is approximate but should be within ~5% for well-behaved distributions
    CHECK_THAT(pq.get(), WithinAbs(499.5, 25.0));
}

TEST_CASE("p2_quantile: p90 of uniform distribution", "[p2_quantile]") {
    p2_quantile pq(0.9);

    for (int i = 0; i < 1000; ++i) {
        pq.push(static_cast<double>(i));
    }

    // True p90 ≈ 899.1
    CHECK_THAT(pq.get(), WithinAbs(899.1, 30.0));
}

TEST_CASE("p2_quantile: p99 of uniform distribution", "[p2_quantile]") {
    p2_quantile pq(0.99);

    for (int i = 0; i < 10000; ++i) {
        pq.push(static_cast<double>(i));
    }

    // True p99 ≈ 9899.01
    CHECK_THAT(pq.get(), WithinAbs(9899.0, 200.0));
}

TEST_CASE("p2_quantile: constant values", "[p2_quantile]") {
    p2_quantile pq(0.5);
    for (int i = 0; i < 100; ++i) {
        pq.push(42.0);
    }
    CHECK_THAT(pq.get(), WithinAbs(42.0, 1e-6));
}

TEST_CASE("p2_quantile: two distinct values", "[p2_quantile]") {
    p2_quantile pq(0.5);
    // 500 zeros then 500 ones
    for (int i = 0; i < 500; ++i) pq.push(0.0);
    for (int i = 0; i < 500; ++i) pq.push(1.0);

    // Median should be near 0.5 (between the two clusters)
    CHECK(pq.get() >= -0.5);
    CHECK(pq.get() <= 1.5);
}

TEST_CASE("p2_quantile: snapshot and restore", "[p2_quantile]") {
    p2_quantile pq(0.5);
    for (int i = 0; i < 50; ++i) pq.push(static_cast<double>(i));

    auto snap = pq.snapshot();

    p2_quantile pq2(0.9); // start with different p
    pq2.restore(snap);

    CHECK(pq2.count() == pq.count());
    CHECK_THAT(pq2.target_quantile(), WithinAbs(0.5, 1e-10));
    CHECK_THAT(pq2.get(), WithinAbs(pq.get(), 1e-10));
}

TEST_CASE("p2_quantile: restore and continue pushing", "[p2_quantile]") {
    p2_quantile pq(0.5);
    for (int i = 0; i < 100; ++i) pq.push(static_cast<double>(i));

    auto snap = pq.snapshot();

    p2_quantile pq2(0.5);
    pq2.restore(snap);

    // Continue pushing more data
    for (int i = 100; i < 200; ++i) pq2.push(static_cast<double>(i));

    // Median of 0..199 ≈ 99.5
    CHECK_THAT(pq2.get(), WithinAbs(99.5, 15.0));
}

TEST_CASE("p2_quantile: p25 of known sequence", "[p2_quantile]") {
    p2_quantile pq(0.25);
    for (int i = 0; i < 1000; ++i) {
        pq.push(static_cast<double>(i));
    }
    // True p25 ≈ 249.75
    CHECK_THAT(pq.get(), WithinAbs(249.75, 20.0));
}
