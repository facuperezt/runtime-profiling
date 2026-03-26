#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <embprof/serialize.hpp>

#include <cstring>
#include <vector>

using namespace embprof;
using Catch::Matchers::WithinAbs;

using test_point = profiling_point<10>;

TEST_CASE("serialize: serialized_size is non-zero", "[serialize]") {
    constexpr auto sz = serialized_size<10, default_clock>();
    CHECK(sz > detail::HEADER_SIZE);
    CHECK(sz == detail::HEADER_SIZE + sizeof(test_point::state_t));
}

TEST_CASE("serialize: roundtrip empty profiling_point", "[serialize]") {
    test_point pp("empty", 0, 1000);

    constexpr auto sz = serialized_size<10, default_clock>();
    std::vector<uint8_t> buf(sz);

    uint32_t written = serialize(pp, buf.data(), static_cast<uint32_t>(buf.size()));
    CHECK(written == sz);

    test_point pp2("empty_restore", 0, 1000);
    bool ok = deserialize(pp2, buf.data(), static_cast<uint32_t>(buf.size()));
    CHECK(ok);
    CHECK(pp2.stats().count() == 0);
}

TEST_CASE("serialize: roundtrip with data", "[serialize]") {
    test_point pp("with_data", 0, 1000);
    for (int i = 0; i < 200; ++i) {
        pp.record(static_cast<double>(i));
    }

    constexpr auto sz = serialized_size<10, default_clock>();
    std::vector<uint8_t> buf(sz);

    uint32_t written = serialize(pp, buf.data(), static_cast<uint32_t>(buf.size()));
    CHECK(written == sz);

    test_point pp2("restored", 0, 1000);
    bool ok = deserialize(pp2, buf.data(), static_cast<uint32_t>(buf.size()));
    REQUIRE(ok);

    CHECK(pp2.stats().count() == 200);
    CHECK_THAT(pp2.stats().mean(), WithinAbs(pp.stats().mean(), 1e-10));
    CHECK_THAT(pp2.stats().variance(), WithinAbs(pp.stats().variance(), 1e-6));
    CHECK_THAT(pp2.stats().min_val(), WithinAbs(pp.stats().min_val(), 1e-10));
    CHECK_THAT(pp2.stats().max_val(), WithinAbs(pp.stats().max_val(), 1e-10));
    CHECK(pp2.hist().total_count() == 200);
    CHECK_THAT(pp2.quantile50().get(), WithinAbs(pp.quantile50().get(), 1e-6));
}

TEST_CASE("serialize: buffer too small returns 0", "[serialize]") {
    test_point pp("small_buf", 0, 1000);
    uint8_t buf[4] = {};
    uint32_t written = serialize(pp, buf, 4);
    CHECK(written == 0);
}

TEST_CASE("serialize: bad magic fails", "[serialize]") {
    test_point pp("bad_magic", 0, 1000);
    pp.record(42);

    constexpr auto sz = serialized_size<10, default_clock>();
    std::vector<uint8_t> buf(sz);
    serialize(pp, buf.data(), static_cast<uint32_t>(buf.size()));

    // Corrupt magic
    buf[0] = 'X';

    test_point pp2("fail", 0, 1000);
    bool ok = deserialize(pp2, buf.data(), static_cast<uint32_t>(buf.size()));
    CHECK_FALSE(ok);
    CHECK(pp2.stats().count() == 0); // should be untouched
}

TEST_CASE("serialize: bad version fails", "[serialize]") {
    test_point pp("bad_ver", 0, 1000);
    pp.record(42);

    constexpr auto sz = serialized_size<10, default_clock>();
    std::vector<uint8_t> buf(sz);
    serialize(pp, buf.data(), static_cast<uint32_t>(buf.size()));

    // Corrupt version
    buf[4] = 99;

    test_point pp2("fail", 0, 1000);
    bool ok = deserialize(pp2, buf.data(), static_cast<uint32_t>(buf.size()));
    CHECK_FALSE(ok);
}

TEST_CASE("serialize: bad payload size fails", "[serialize]") {
    test_point pp("bad_size", 0, 1000);
    pp.record(42);

    constexpr auto sz = serialized_size<10, default_clock>();
    std::vector<uint8_t> buf(sz);
    serialize(pp, buf.data(), static_cast<uint32_t>(buf.size()));

    // Corrupt payload size
    buf[5] = 0;
    buf[6] = 0;

    test_point pp2("fail", 0, 1000);
    bool ok = deserialize(pp2, buf.data(), static_cast<uint32_t>(buf.size()));
    CHECK_FALSE(ok);
}

TEST_CASE("serialize: deserialize with short buffer fails", "[serialize]") {
    test_point pp("short", 0, 1000);
    pp.record(42);

    constexpr auto sz = serialized_size<10, default_clock>();
    std::vector<uint8_t> buf(sz);
    serialize(pp, buf.data(), static_cast<uint32_t>(buf.size()));

    test_point pp2("fail", 0, 1000);
    // Pass truncated buffer
    bool ok = deserialize(pp2, buf.data(), detail::HEADER_SIZE - 1);
    CHECK_FALSE(ok);
}

TEST_CASE("serialize: continue recording after deserialize", "[serialize]") {
    test_point pp("continue", 0, 1000);
    for (int i = 0; i < 50; ++i) pp.record(static_cast<double>(i));

    constexpr auto sz = serialized_size<10, default_clock>();
    std::vector<uint8_t> buf(sz);
    serialize(pp, buf.data(), static_cast<uint32_t>(buf.size()));

    test_point pp2("continued", 0, 1000);
    deserialize(pp2, buf.data(), static_cast<uint32_t>(buf.size()));

    // Push more data
    for (int i = 50; i < 100; ++i) pp2.record(static_cast<double>(i));

    CHECK(pp2.stats().count() == 100);
    CHECK_THAT(pp2.stats().mean(), WithinAbs(49.5, 1e-6));
    CHECK(pp2.hist().total_count() == 100);
}

TEST_CASE("serialize: header bytes are correct", "[serialize]") {
    test_point pp("header_check", 0, 1000);

    constexpr auto sz = serialized_size<10, default_clock>();
    std::vector<uint8_t> buf(sz);
    serialize(pp, buf.data(), static_cast<uint32_t>(buf.size()));

    CHECK(buf[0] == 'E');
    CHECK(buf[1] == 'P');
    CHECK(buf[2] == 'R');
    CHECK(buf[3] == 'F');
    CHECK(buf[4] == 1); // version
}
