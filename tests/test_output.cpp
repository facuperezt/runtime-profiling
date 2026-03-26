#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <embprof/output.hpp>
#include <embprof/formatter.hpp>
#include <embprof/registry.hpp>

#include <string>
#include <cstdint>
#include <climits>

using namespace embprof;
using Catch::Matchers::ContainsSubstring;

// ---------------------------------------------------------------------------
// Test helper: captures all output into a std::string (test-only, uses heap).
// ---------------------------------------------------------------------------
struct capture_sink {
    std::string buf;
    void put_char(char c) { buf += c; }
    void write(const char* s, uint32_t len) { buf.append(s, len); }
};

// ---------------------------------------------------------------------------
// fmt::write_u32
// ---------------------------------------------------------------------------

TEST_CASE("fmt::write_u32 — zero", "[output][fmt]") {
    capture_sink s;
    fmt::write_u32(s, 0u);
    CHECK(s.buf == "0");
}

TEST_CASE("fmt::write_u32 — small values", "[output][fmt]") {
    capture_sink s;
    fmt::write_u32(s, 1u);
    CHECK(s.buf == "1");

    capture_sink s2;
    fmt::write_u32(s2, 42u);
    CHECK(s2.buf == "42");
}

TEST_CASE("fmt::write_u32 — large values", "[output][fmt]") {
    capture_sink s;
    fmt::write_u32(s, 1000000u);
    CHECK(s.buf == "1000000");

    capture_sink s2;
    fmt::write_u32(s2, 4294967295u); // UINT32_MAX
    CHECK(s2.buf == "4294967295");
}

// ---------------------------------------------------------------------------
// fmt::write_i32
// ---------------------------------------------------------------------------

TEST_CASE("fmt::write_i32 — zero and positive", "[output][fmt]") {
    capture_sink s;
    fmt::write_i32(s, 0);
    CHECK(s.buf == "0");

    capture_sink s2;
    fmt::write_i32(s2, 42);
    CHECK(s2.buf == "42");

    capture_sink s3;
    fmt::write_i32(s3, 2147483647); // INT32_MAX
    CHECK(s3.buf == "2147483647");
}

TEST_CASE("fmt::write_i32 — negative values", "[output][fmt]") {
    capture_sink s;
    fmt::write_i32(s, -1);
    CHECK(s.buf == "-1");

    capture_sink s2;
    fmt::write_i32(s2, -42);
    CHECK(s2.buf == "-42");
}

TEST_CASE("fmt::write_i32 — INT32_MIN", "[output][fmt]") {
    capture_sink s;
    fmt::write_i32(s, (-2147483647 - 1)); // INT32_MIN
    CHECK(s.buf == "-2147483648");
}

// ---------------------------------------------------------------------------
// fmt::write_float
// ---------------------------------------------------------------------------

TEST_CASE("fmt::write_float — zero", "[output][fmt]") {
    capture_sink s;
    fmt::write_float(s, 0.0);
    CHECK(s.buf == "0.00");
}

TEST_CASE("fmt::write_float — positive values", "[output][fmt]") {
    capture_sink s;
    fmt::write_float(s, 1.5);
    CHECK(s.buf == "1.50");

    capture_sink s2;
    fmt::write_float(s2, 3.14, 2);
    CHECK(s2.buf == "3.14");
}

TEST_CASE("fmt::write_float — negative values", "[output][fmt]") {
    capture_sink s;
    fmt::write_float(s, -3.14, 2);
    CHECK(s.buf == "-3.14");

    capture_sink s2;
    fmt::write_float(s2, -0.5, 1);
    CHECK(s2.buf == "-0.5");
}

TEST_CASE("fmt::write_float — different decimal places", "[output][fmt]") {
    capture_sink s0;
    fmt::write_float(s0, 3.14159, 0);
    CHECK(s0.buf == "3");

    capture_sink s1;
    fmt::write_float(s1, 3.14159, 1);
    CHECK(s1.buf == "3.1");

    capture_sink s3;
    fmt::write_float(s3, 3.14159, 3);
    CHECK(s3.buf == "3.142");
}

TEST_CASE("fmt::write_float — large values", "[output][fmt]") {
    capture_sink s;
    fmt::write_float(s, 99999.99, 2);
    CHECK(s.buf == "99999.99");
}

// ---------------------------------------------------------------------------
// fmt::write_str
// ---------------------------------------------------------------------------

TEST_CASE("fmt::write_str — empty string", "[output][fmt]") {
    capture_sink s;
    fmt::write_str(s, "");
    CHECK(s.buf.empty());
}

TEST_CASE("fmt::write_str — hello", "[output][fmt]") {
    capture_sink s;
    fmt::write_str(s, "hello");
    CHECK(s.buf == "hello");
}

TEST_CASE("fmt::write_str — null pointer", "[output][fmt]") {
    capture_sink s;
    fmt::write_str(s, nullptr);
    CHECK(s.buf.empty());
}

TEST_CASE("fmt::write_str — with newlines", "[output][fmt]") {
    capture_sink s;
    fmt::write_str(s, "line1\nline2");
    CHECK(s.buf == "line1\nline2");
}

// ---------------------------------------------------------------------------
// fmt::write_newline
// ---------------------------------------------------------------------------

TEST_CASE("fmt::write_newline", "[output][fmt]") {
    capture_sink s;
    fmt::write_newline(s);
    CHECK(s.buf == "\n");
}

// ---------------------------------------------------------------------------
// null_sink — just verify it compiles and doesn't crash
// ---------------------------------------------------------------------------

TEST_CASE("null_sink compiles and is no-op", "[output][sink]") {
    null_sink ns;
    ns.put_char('x');
    ns.write("hello", 5);
    // If we got here without crashing, the test passes.
    CHECK(true);
}

// ---------------------------------------------------------------------------
// itm_sink<0> — on desktop it should be a no-op, just verify compilation
// ---------------------------------------------------------------------------

TEST_CASE("itm_sink<0> compiles on desktop (no-op)", "[output][sink]") {
    itm_sink<0> itm;
    itm.put_char('A');
    itm.write("test", 4);
    // No crash = pass on non-ARM
    CHECK(true);
}

// ---------------------------------------------------------------------------
// callback_sink
// ---------------------------------------------------------------------------

TEST_CASE("callback_sink captures output", "[output][sink]") {
    static std::string captured;
    captured.clear();

    callback_sink cs([](const char* data, uint32_t len) {
        captured.append(data, len);
    });

    cs.put_char('H');
    cs.write("ello", 4);
    CHECK(captured == "Hello");
}

TEST_CASE("callback_sink with null function pointer", "[output][sink]") {
    callback_sink cs(nullptr);
    // Should not crash
    cs.put_char('x');
    cs.write("test", 4);
    CHECK(true);
}

// ---------------------------------------------------------------------------
// report() — full multi-line report
// ---------------------------------------------------------------------------

using test_point = profiling_point<10>;

TEST_CASE("report — profiling point with known data", "[output][report]") {
    test_point pp("motor_control", 100.0, 10000.0, bucket_mode::linear);

    // Feed known data
    for (int i = 0; i < 100; ++i) {
        pp.record(static_cast<double>(100 + i * 50));
    }

    capture_sink s;
    report(s, pp);

    // Verify expected substrings in output
    CHECK_THAT(s.buf, ContainsSubstring("[motor_control]"));
    CHECK_THAT(s.buf, ContainsSubstring("count : 100"));
    CHECK_THAT(s.buf, ContainsSubstring("mean"));
    CHECK_THAT(s.buf, ContainsSubstring("min"));
    CHECK_THAT(s.buf, ContainsSubstring("max"));
    CHECK_THAT(s.buf, ContainsSubstring("stddev"));
    CHECK_THAT(s.buf, ContainsSubstring("p50"));
    CHECK_THAT(s.buf, ContainsSubstring("p90"));
    CHECK_THAT(s.buf, ContainsSubstring("p99"));
    CHECK_THAT(s.buf, ContainsSubstring("histogram"));
    CHECK_THAT(s.buf, ContainsSubstring("10 buckets"));
}

TEST_CASE("report — empty profiling point", "[output][report]") {
    test_point pp("empty_point", 0.0, 1000.0);

    capture_sink s;
    report(s, pp);

    CHECK_THAT(s.buf, ContainsSubstring("[empty_point]"));
    CHECK_THAT(s.buf, ContainsSubstring("count : 0"));
    CHECK_THAT(s.buf, ContainsSubstring("(no data)"));
    // Should NOT contain histogram for empty point
    CHECK_THAT(s.buf, !ContainsSubstring("histogram"));
}

TEST_CASE("report — single sample", "[output][report]") {
    test_point pp("single_sample", 0.0, 1000.0);
    pp.record(500.0);

    capture_sink s;
    report(s, pp);

    CHECK_THAT(s.buf, ContainsSubstring("[single_sample]"));
    CHECK_THAT(s.buf, ContainsSubstring("count : 1"));
    CHECK_THAT(s.buf, ContainsSubstring("mean"));
    CHECK_THAT(s.buf, ContainsSubstring("histogram"));
}

// ---------------------------------------------------------------------------
// report — histogram formatting (underflow, overflow, bucket lines)
// ---------------------------------------------------------------------------

TEST_CASE("report — histogram shows underflow and overflow", "[output][report]") {
    test_point pp("hist_test", 100.0, 1000.0, bucket_mode::linear);

    // Add underflow values
    pp.record(50.0);
    pp.record(10.0);

    // Add in-range values
    for (int i = 0; i < 10; ++i) {
        pp.record(500.0);
    }

    // Add overflow values
    pp.record(2000.0);
    pp.record(5000.0);
    pp.record(9999.0);

    capture_sink s;
    report(s, pp);

    // Check underflow line present
    CHECK_THAT(s.buf, ContainsSubstring("<100.00"));
    // Check overflow line present
    CHECK_THAT(s.buf, ContainsSubstring(">=1000.00"));
    // Check bucket lines with boundaries
    CHECK_THAT(s.buf, ContainsSubstring("["));
    CHECK_THAT(s.buf, ContainsSubstring(")"));
}

// ---------------------------------------------------------------------------
// report_summary() — single-line format
// ---------------------------------------------------------------------------

TEST_CASE("report_summary — single line format", "[output][report]") {
    test_point pp("motor_loop", 0.0, 10000.0);
    for (int i = 0; i < 50; ++i) {
        pp.record(static_cast<double>(1000 + i * 10));
    }

    capture_sink s;
    report_summary(s, pp);

    CHECK_THAT(s.buf, ContainsSubstring("[motor_loop]"));
    CHECK_THAT(s.buf, ContainsSubstring("n=50"));
    CHECK_THAT(s.buf, ContainsSubstring("mean="));
    CHECK_THAT(s.buf, ContainsSubstring("p50="));
    CHECK_THAT(s.buf, ContainsSubstring("p90="));
    CHECK_THAT(s.buf, ContainsSubstring("p99="));

    // Should be single line (one newline at the end)
    auto newline_count = std::count(s.buf.begin(), s.buf.end(), '\n');
    CHECK(newline_count == 1);
}

// ---------------------------------------------------------------------------
// report_all() — iterates registry
// ---------------------------------------------------------------------------

TEST_CASE("report_all — reports all registered points", "[output][report]") {
    auto& reg = registry::instance();
    reg.clear();

    test_point pp1("point_alpha", 0.0, 1000.0);
    test_point pp2("point_beta", 0.0, 1000.0);

    pp1.record(100.0);
    pp2.record(200.0);
    pp2.record(300.0);

    registered_point<10> rp1(pp1);
    registered_point<10> rp2(pp2);

    capture_sink s;
    report_all(s);

    CHECK_THAT(s.buf, ContainsSubstring("[point_alpha]"));
    CHECK_THAT(s.buf, ContainsSubstring("[point_beta]"));
    CHECK_THAT(s.buf, ContainsSubstring("count : 1"));
    CHECK_THAT(s.buf, ContainsSubstring("count : 2"));

    reg.clear();
}

// ---------------------------------------------------------------------------
// callback_sink with formatter
// ---------------------------------------------------------------------------

TEST_CASE("callback_sink works with formatter functions", "[output][sink]") {
    static std::string captured;
    captured.clear();

    callback_sink cs([](const char* data, uint32_t len) {
        captured.append(data, len);
    });

    fmt::write_str(cs, "count=");
    fmt::write_u32(cs, 42u);
    fmt::write_newline(cs);

    CHECK(captured == "count=42\n");
}
