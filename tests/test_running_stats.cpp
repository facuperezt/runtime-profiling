#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <embprof/running_stats.hpp>

#include <cmath>
#include <vector>

using namespace embprof;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE("running_stats: default constructed state", "[running_stats]") {
    running_stats rs;
    CHECK(rs.count() == 0);
    CHECK(rs.min_val() == 0);
    CHECK(rs.max_val() == 0);
    CHECK(rs.mean() == 0);
    CHECK(rs.variance() == 0);
    CHECK(rs.stddev() == 0);
}

TEST_CASE("running_stats: single value", "[running_stats]") {
    running_stats rs;
    rs.push(42.0);
    CHECK(rs.count() == 1);
    CHECK_THAT(rs.mean(), WithinAbs(42.0, 1e-10));
    CHECK_THAT(rs.min_val(), WithinAbs(42.0, 1e-10));
    CHECK_THAT(rs.max_val(), WithinAbs(42.0, 1e-10));
    CHECK(rs.variance() == 0); // undefined for n=1, returns 0
    CHECK_THAT(rs.last(), WithinAbs(42.0, 1e-10));
}

TEST_CASE("running_stats: known sequence", "[running_stats]") {
    // Values: 2, 4, 4, 4, 5, 5, 7, 9
    // Mean = 5.0, Variance (sample) = 4.571..., StdDev = 2.138...
    running_stats rs;
    double vals[] = {2, 4, 4, 4, 5, 5, 7, 9};
    for (double v : vals) rs.push(v);

    CHECK(rs.count() == 8);
    CHECK_THAT(rs.mean(), WithinAbs(5.0, 1e-10));
    CHECK_THAT(rs.min_val(), WithinAbs(2.0, 1e-10));
    CHECK_THAT(rs.max_val(), WithinAbs(9.0, 1e-10));
    CHECK_THAT(rs.variance(), WithinRel(4.571428571, 1e-6));
    CHECK_THAT(rs.stddev(), WithinRel(2.138089935, 1e-6));
    CHECK_THAT(rs.last(), WithinAbs(9.0, 1e-10));
}

TEST_CASE("running_stats: population vs sample variance", "[running_stats]") {
    running_stats rs;
    rs.push(10); rs.push(20); rs.push(30);
    // Population variance = ((10-20)^2 + 0 + (30-20)^2) / 3 = 200/3 = 66.667
    // Sample variance = 200/2 = 100
    CHECK_THAT(rs.variance_population(), WithinRel(66.6666667, 1e-5));
    CHECK_THAT(rs.variance(), WithinAbs(100.0, 1e-10));
}

TEST_CASE("running_stats: reset clears everything", "[running_stats]") {
    running_stats rs;
    rs.push(1); rs.push(2); rs.push(3);
    rs.reset();
    CHECK(rs.count() == 0);
    CHECK(rs.mean() == 0);
    CHECK(rs.variance() == 0);
}

TEST_CASE("running_stats: numerical stability with large offset", "[running_stats]") {
    // Welford should handle values clustered around a large number without
    // catastrophic cancellation.
    running_stats rs;
    const double base = 1e9;
    double vals[] = {base + 1, base + 2, base + 3, base + 4, base + 5};
    for (double v : vals) rs.push(v);

    // Mean should be base + 3
    CHECK_THAT(rs.mean(), WithinAbs(base + 3.0, 1e-4));
    // Variance of {1,2,3,4,5} offset = sample var = 2.5
    CHECK_THAT(rs.variance(), WithinRel(2.5, 1e-6));
}

TEST_CASE("running_stats: merge two sets", "[running_stats]") {
    running_stats a, b;
    a.push(1); a.push(2); a.push(3);
    b.push(4); b.push(5); b.push(6);

    a.merge(b);

    CHECK(a.count() == 6);
    CHECK_THAT(a.mean(), WithinAbs(3.5, 1e-10));
    CHECK_THAT(a.min_val(), WithinAbs(1.0, 1e-10));
    CHECK_THAT(a.max_val(), WithinAbs(6.0, 1e-10));
    // Variance of {1,2,3,4,5,6}: sample var = 3.5
    CHECK_THAT(a.variance(), WithinRel(3.5, 1e-6));
}

TEST_CASE("running_stats: merge into empty", "[running_stats]") {
    running_stats a, b;
    b.push(10); b.push(20);
    a.merge(b);
    CHECK(a.count() == 2);
    CHECK_THAT(a.mean(), WithinAbs(15.0, 1e-10));
}

TEST_CASE("running_stats: merge empty into existing", "[running_stats]") {
    running_stats a, b;
    a.push(10); a.push(20);
    a.merge(b);
    CHECK(a.count() == 2);
    CHECK_THAT(a.mean(), WithinAbs(15.0, 1e-10));
}

TEST_CASE("running_stats: snapshot and restore roundtrip", "[running_stats]") {
    running_stats rs;
    rs.push(3); rs.push(7); rs.push(11);

    auto snap = rs.snapshot();

    running_stats rs2;
    rs2.restore(snap);

    CHECK(rs2.count() == rs.count());
    CHECK_THAT(rs2.mean(), WithinAbs(rs.mean(), 1e-10));
    CHECK_THAT(rs2.variance(), WithinAbs(rs.variance(), 1e-10));
    CHECK_THAT(rs2.min_val(), WithinAbs(rs.min_val(), 1e-10));
    CHECK_THAT(rs2.max_val(), WithinAbs(rs.max_val(), 1e-10));
}

TEST_CASE("running_stats: large count", "[running_stats]") {
    running_stats rs;
    // Push 0..999 — mean = 499.5, var = 83416.667 (sample)
    for (uint32_t i = 0; i < 1000; ++i) {
        rs.push(static_cast<double>(i));
    }
    CHECK(rs.count() == 1000);
    CHECK_THAT(rs.mean(), WithinAbs(499.5, 1e-6));
    // Sample variance of uniform {0..999} = n*(n+1)/12 * n/(n-1) ≈ 83416.667
    // Exact: (1000^2 - 1)/12 = 999999/12 = 83333.25
    CHECK_THAT(rs.variance(), WithinRel(83333.25 * 1000.0 / 999.0, 1e-4));
}
