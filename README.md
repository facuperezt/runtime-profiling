# embprof

Lightweight, header-only C++14/17 runtime profiling library designed for embedded systems.

No heap allocation. No exceptions. No RTTI. Fixed memory footprint per profiling point.

## Features

| Feature | Description |
|---|---|
| **Running statistics** | Online mean, variance, stddev, min, max via Welford's algorithm — O(1) memory |
| **Histogram** | Fixed-bucket histograms with linear or log-linear spacing. Configurable bucket count at compile time |
| **P² quantile estimation** | Online p50/p90/p99 estimation using 5 markers (~80 bytes) — no observation storage |
| **RAII scoped timer** | Measure any code block with zero-cost RAII or manual start/stop |
| **Pluggable clocks** | DWT cycle counter, SysTick, std::chrono, or bring your own |
| **Compact serialization** | Binary state export/import for sending over CAN, UART, or storing in NVRAM |
| **Global registry** | Enumerate all profiling points at runtime for reporting |
| **Compile-out support** | `EMBPROF_DISABLE=1` removes all instrumentation at zero cost |

## Requirements

- C++14 compiler (C++17 optional, enables `[[nodiscard]]`, `inline` variables, `if constexpr`)
- CMake 3.14+
- No standard library dependencies on embedded targets (disable `EMBPROF_NO_STD_CHRONO` and provide your own clock)

## Quick Start

```cpp
#include <embprof/embprof.hpp>

// Create a profiling point: name, histogram range, bucket mode
static embprof::profiling_point<20> my_func(
    "my_func", 100.0, 100000.0, embprof::bucket_mode::log_linear);

void some_function() {
    EMBPROF_SCOPE(my_func);  // RAII — measures until end of scope
    // ... your code ...
}

// Or manual start/stop:
void another_function() {
    EMBPROF_START(my_func);
    // ... your code ...
    EMBPROF_STOP(my_func);
}

// Read results:
auto& s = my_func.stats();
printf("count=%u mean=%.1f stddev=%.1f\n", s.count(), s.mean(), s.stddev());
printf("p50=%.1f p99=%.1f\n", my_func.quantile50().get(), my_func.quantile99().get());
```

## Integration

### CMake (FetchContent)

```cmake
include(FetchContent)
FetchContent_Declare(embprof
    GIT_REPOSITORY https://github.com/facuperezt/runtime-profiling.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(embprof)

target_link_libraries(your_target PRIVATE embprof)
```

### CMake (add_subdirectory)

```cmake
add_subdirectory(path/to/embprof)
target_link_libraries(your_target PRIVATE embprof)
```

### Copy headers

Just copy the `include/embprof/` directory into your project. It's header-only.

## Clock Configuration

The library needs a clock source. Select one via compile definition:

| Define | Clock | Notes |
|---|---|---|
| *(default)* | `std::chrono::steady_clock` | Desktop/testing — wraps to nanoseconds |
| `EMBPROF_CLOCK_DWT` | ARM DWT CYCCNT | Cortex-M3/M4/M7 — you must enable DWT first |
| `EMBPROF_CLOCK_SYSTICK` | SysTick->VAL | Counts down — library handles inversion |
| `EMBPROF_CLOCK_USER` | `embprof::user_clock` | Define your own struct with `static tick_t now()` |
| `EMBPROF_NO_STD_CHRONO` | `null_clock` | Disables `<chrono>` include entirely |

### DWT setup (Cortex-M)

```c
CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
DWT->CYCCNT = 0;
DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
```

### Custom clock example

```cpp
// Define before including embprof headers, or in a separate header:
namespace embprof {
    struct user_clock {
        static tick_t now() noexcept {
            return MY_TIMER->CNT;  // your hardware timer
        }
    };
}
// Then compile with -DEMBPROF_CLOCK_USER
```

## Compile-Time Configuration

| Define | Default | Description |
|---|---|---|
| `EMBPROF_TICK_TYPE` | `uint32_t` | Underlying tick type |
| `EMBPROF_FLOAT_TYPE` | `double` | Accumulator float type (use `float` to save RAM) |
| `EMBPROF_MAX_PROFILING_POINTS` | `32` | Registry capacity (0 disables registry) |
| `EMBPROF_DEFAULT_HISTOGRAM_BUCKETS` | `20` | Default bucket count |
| `EMBPROF_DISABLE` | `0` | Set to 1 to compile out all instrumentation |

## Serialization

Export state for sending over CAN/UART or storing in NVRAM:

```cpp
constexpr auto buf_size = embprof::serialized_size<20, embprof::default_clock>();
uint8_t buf[buf_size];

// Serialize
uint32_t written = embprof::serialize(my_point, buf, buf_size);
send_over_can(buf, written);

// Deserialize (e.g., on a host or after power cycle)
embprof::profiling_point<20> restored("restored", 100, 100000);
if (embprof::deserialize(restored, buf, written)) {
    // restored now has the full state — stats, histogram, quantiles
    // You can keep recording into it.
}
```

Format: `[EPRF magic: 4B][version: 1B][payload_size: 2B][state struct]`

## Memory Usage

Per profiling point with default settings (20 buckets, `double` floats, p50+p90+p99):

| Component | Bytes |
|---|---|
| Running stats | ~52 |
| Histogram (20 buckets) | ~264 |
| P² quantile × 3 | ~360 |
| Overhead (name ptr, etc.) | ~16 |
| **Total** | **~692** |

Use `float` instead of `double` (`-DEMBPROF_FLOAT_TYPE=float`) and fewer buckets to reduce this.

## Building Tests

```bash
cmake -B build -DEMBPROF_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Building Examples

```bash
cmake -B build -DEMBPROF_BUILD_EXAMPLES=ON
cmake --build build
./build/embprof_basic_example
```

## License

MIT
