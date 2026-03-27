cmake_minimum_required(VERSION 3.14)
project(embprof
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "Lightweight C++14/17 runtime profiling for embedded systems"
)

# ---------------------------------------------------------------------------
# Library (header-only)
# ---------------------------------------------------------------------------
add_library(embprof INTERFACE)
target_include_directories(embprof INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(embprof INTERFACE cxx_std_14)

# Optional: request C++17 features
option(EMBPROF_USE_CPP17 "Compile with C++17 features enabled" OFF)
if(EMBPROF_USE_CPP17)
    target_compile_features(embprof INTERFACE cxx_std_17)
endif()

# Configurable defines
option(EMBPROF_DISABLE "Compile out all profiling instrumentation" OFF)
if(EMBPROF_DISABLE)
    target_compile_definitions(embprof INTERFACE EMBPROF_DISABLE=1)
endif()

set(EMBPROF_MAX_PROFILING_POINTS "32" CACHE STRING "Max registry slots")
target_compile_definitions(embprof INTERFACE
    EMBPROF_MAX_PROFILING_POINTS=${EMBPROF_MAX_PROFILING_POINTS}
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
option(EMBPROF_BUILD_TESTS "Build unit tests" ON)

if(EMBPROF_BUILD_TESTS)
    include(FetchContent)

    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.2
    )
    FetchContent_MakeAvailable(Catch2)

    add_executable(embprof_tests
        tests/test_running_stats.cpp
        tests/test_histogram.cpp
        tests/test_p2_quantile.cpp
        tests/test_profiling_point.cpp
        tests/test_registry.cpp
        tests/test_serialize.cpp
        tests/test_scoped_timer.cpp
        tests/test_output.cpp
    )
    target_link_libraries(embprof_tests PRIVATE embprof Catch2::Catch2WithMain)
    target_compile_features(embprof_tests PRIVATE cxx_std_17)

    # Lite header gets its own executable to avoid ODR conflicts
    add_executable(embprof_lite_tests tests/test_lite_standalone.cpp)
    target_link_libraries(embprof_lite_tests PRIVATE Catch2::Catch2WithMain)
    target_compile_features(embprof_lite_tests PRIVATE cxx_std_17)

    include(CTest)
    include(Catch)
    catch_discover_tests(embprof_tests)
    catch_discover_tests(embprof_lite_tests)
endif()

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
option(EMBPROF_BUILD_EXAMPLES "Build example programs" OFF)
if(EMBPROF_BUILD_EXAMPLES)
    add_executable(embprof_basic_example examples/basic_usage.cpp)
    target_link_libraries(embprof_basic_example PRIVATE embprof)
    target_compile_features(embprof_basic_example PRIVATE cxx_std_14)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS embprof EXPORT embprof-targets)
install(DIRECTORY include/embprof DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT embprof-targets
    FILE embprof-config.cmake
    NAMESPACE embprof::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/embprof
)
