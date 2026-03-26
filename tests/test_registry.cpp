#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <embprof/registry.hpp>

#include <string>
#include <vector>

using namespace embprof;
using Catch::Matchers::WithinAbs;

// Use chrono_clock (the default on desktop) for these tests.
using test_point = profiling_point<10>;

TEST_CASE("registry: starts empty", "[registry]") {
    auto& reg = registry::instance();
    reg.clear();
    CHECK(reg.size() == 0);
}

TEST_CASE("registry: add and retrieve", "[registry]") {
    auto& reg = registry::instance();
    reg.clear();

    test_point pp1("func_a", 0, 1000);
    test_point pp2("func_b", 0, 1000);

    profiling_point_adapter<10, default_clock> a1(pp1);
    profiling_point_adapter<10, default_clock> a2(pp2);

    CHECK(reg.add(&a1));
    CHECK(reg.add(&a2));
    CHECK(reg.size() == 2);

    CHECK(std::string(reg.get(0)->name()) == "func_a");
    CHECK(std::string(reg.get(1)->name()) == "func_b");
}

TEST_CASE("registry: get out of bounds returns nullptr", "[registry]") {
    auto& reg = registry::instance();
    reg.clear();
    CHECK(reg.get(0) == nullptr);
    CHECK(reg.get(999) == nullptr);
}

TEST_CASE("registry: for_each iterates all", "[registry]") {
    auto& reg = registry::instance();
    reg.clear();

    test_point pp1("iter_a", 0, 1000);
    test_point pp2("iter_b", 0, 1000);

    profiling_point_adapter<10, default_clock> a1(pp1);
    profiling_point_adapter<10, default_clock> a2(pp2);
    reg.add(&a1);
    reg.add(&a2);

    std::vector<std::string> names;
    reg.for_each([&](profiling_point_base& pp) {
        names.push_back(pp.name());
    });

    REQUIRE(names.size() == 2);
    CHECK(names[0] == "iter_a");
    CHECK(names[1] == "iter_b");
}

TEST_CASE("registry: reset_all clears stats", "[registry]") {
    auto& reg = registry::instance();
    reg.clear();

    test_point pp("resetable", 0, 1000);
    pp.record(42);

    profiling_point_adapter<10, default_clock> adapter(pp);
    reg.add(&adapter);

    CHECK(pp.stats().count() == 1);
    reg.reset_all();
    CHECK(pp.stats().count() == 0);
}

TEST_CASE("registry: registered_point RAII helper", "[registry]") {
    auto& reg = registry::instance();
    reg.clear();

    test_point pp("raii_registered", 0, 1000);
    registered_point<10> rp(pp);

    CHECK(reg.size() == 1);
    CHECK(std::string(reg.get(0)->name()) == "raii_registered");
}

TEST_CASE("registry: virtual interface returns stats", "[registry]") {
    auto& reg = registry::instance();
    reg.clear();

    test_point pp("stat_check", 0, 1000);
    for (int i = 0; i < 100; ++i) pp.record(static_cast<double>(i));

    profiling_point_adapter<10, default_clock> adapter(pp);
    reg.add(&adapter);

    auto* base = reg.get(0);
    REQUIRE(base != nullptr);
    CHECK(base->stats().count() == 100);
    CHECK_THAT(base->stats().mean(), WithinAbs(49.5, 1e-6));
}

TEST_CASE("registry: clear empties the registry", "[registry]") {
    auto& reg = registry::instance();
    reg.clear();

    test_point pp("to_clear", 0, 1000);
    profiling_point_adapter<10, default_clock> adapter(pp);
    reg.add(&adapter);

    CHECK(reg.size() == 1);
    reg.clear();
    CHECK(reg.size() == 0);
}
