# Timing Embedded Software on Target: A Practical Guide

This guide covers everything you need to know about measuring execution time in embedded systems — what to measure, how to measure it, what can go wrong, and how to interpret the results.

---

## Table of Contents

1. [Choosing a Clock Source](#1-choosing-a-clock-source)
2. [Setting Up the DWT Cycle Counter](#2-setting-up-the-dwt-cycle-counter)
3. [Measurement Overhead and How to Account for It](#3-measurement-overhead-and-how-to-account-for-it)
4. [The Compiler Will Fight You](#4-the-compiler-will-fight-you)
5. [Caches, Flash Wait States, and Why Your Numbers Vary](#5-caches-flash-wait-states-and-why-your-numbers-vary)
6. [Interrupts and Preemption](#6-interrupts-and-preemption)
7. [Counter Overflow and Wraparound](#7-counter-overflow-and-wraparound)
8. [What to Measure](#8-what-to-measure)
9. [Statistical Thinking](#9-statistical-thinking)
10. [Online vs. Offline Analysis](#10-online-vs-offline-analysis)
11. [Getting Data Off the Target](#11-getting-data-off-the-target)
12. [Common Pitfalls](#12-common-pitfalls)
13. [Recommended Workflow](#13-recommended-workflow)
14. [embprof Quick Reference](#14-embprof-quick-reference)

---

## 1. Choosing a Clock Source

Not all timers are equal. Your choice of clock source directly determines measurement resolution and overhead.

### DWT CYCCNT (Recommended)

The Data Watchpoint and Trace unit's cycle counter is the gold standard for Cortex-M3/M4/M7 profiling:

- **Resolution:** 1 CPU cycle
- **Overhead:** ~1 cycle to read (single memory-mapped register read)
- **Width:** 32 bits, wraps at 2^32 cycles
- **Availability:** Cortex-M3, M4, M7 (NOT Cortex-M0/M0+)

At 160 MHz, the counter wraps every ~26.8 seconds. That's plenty for function-level timing.

### SysTick

- **Resolution:** 1 CPU cycle (typically clocked from core clock)
- **Gotcha:** Counts DOWN, not up. Elapsed = `start - stop`, not `stop - start`
- **Width:** 24 bits — wraps every ~0.1 seconds at 160 MHz
- **Use when:** DWT is unavailable or you need a timer that's always running for the RTOS tick anyway

### Hardware Timer Peripheral (TIM, PIT, etc.)

- **Resolution:** Depends on prescaler configuration
- **Gotcha:** Must be configured before use, prescaler choice affects resolution vs. range
- **Use when:** You need a timer independent of the core clock (e.g., measuring time while the CPU sleeps)

### Recommendation

Use DWT CYCCNT. It's free, zero-config (after one-time enable), highest resolution, and doesn't consume a timer peripheral.

---

## 2. Setting Up the DWT Cycle Counter

```c
// One-time initialization — call once at startup, before any measurements
void dwt_init(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;  // Enable trace
    DWT->CYCCNT = 0;                                    // Reset counter
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;              // Enable counter
}
```

**Important:** Some debuggers (notably J-Link) may enable the DWT automatically when you connect. This means your timing code might appear to work during debugging but fail in standalone operation if you forget `dwt_init()`.

### Verification

After calling `dwt_init()`, read `DWT->CYCCNT` twice in succession. The second read should be a few cycles greater than the first. If both return 0, trace isn't enabled or the DWT isn't implemented on your core.

---

## 3. Measurement Overhead and How to Account for It

Every measurement adds overhead. Knowing the overhead lets you subtract it or decide if it matters.

### Overhead Budget

| Operation | Typical Cost (Cortex-M4) |
|---|---|
| Read DWT->CYCCNT | 1-2 cycles |
| Function call overhead (non-inlined) | 4-8 cycles |
| RAII constructor + destructor | 2-6 cycles (usually inlined) |
| Full embprof `record()` (stats + histogram + 3x quantile) | ~50-100 cycles |

### Calibrating Overhead

```cpp
// Measure the cost of measurement itself
embprof::profiling_point<> overhead("overhead", 0, 100);
for (int i = 0; i < 10000; ++i) {
    embprof::scoped_timer<> t(overhead);
    // empty — measures only the profiler overhead
}
// overhead.stats().mean() gives you the per-measurement cost
```

### When to Care

- **Function takes >10,000 cycles:** Overhead is noise. Don't think about it.
- **Function takes 100-10,000 cycles:** Overhead is measurable but small (<1%). Note it.
- **Function takes <100 cycles:** Overhead dominates. Use raw DWT reads instead of the full profiler, or subtract calibrated overhead from results.

---

## 4. The Compiler Will Fight You

Optimizing compilers can reorder, eliminate, or transform your code in ways that invalidate timing measurements.

### Problem: Code Reordering

The compiler is allowed to move your clock reads across function calls if it can prove (under the as-if rule) that the observable behavior doesn't change. Since DWT->CYCCNT is `volatile`, the read itself won't be eliminated, but the compiler might move the *code being measured* outside the timed region.

### Solution: Compiler Barriers

```c
// GCC/Clang compiler barrier — prevents reordering across this point
#define COMPILER_BARRIER() __asm volatile("" ::: "memory")
```

The `scoped_timer` in embprof handles this implicitly because:
1. The constructor reads the clock and stores to a member (side effect)
2. The destructor reads the clock again
3. The measured code has observable side effects (writes to volatile memory, calls external functions)

But if you're measuring a pure computation with no side effects, the compiler might move it entirely. In that case, either:
- Use the result (assign to a `volatile` variable)
- Insert a `COMPILER_BARRIER()` after the start read and before the stop read

### Problem: Dead Code Elimination

If the compiler proves the function's result is unused, it may eliminate the entire function call — your timer measures nothing.

```cpp
// BAD: compiler may eliminate the call entirely
{
    scoped_timer t(point);
    expensive_computation(input);  // result unused
}

// GOOD: force the compiler to keep the result
volatile int sink;
{
    scoped_timer t(point);
    sink = expensive_computation(input);
}
```

### Problem: Link-Time Optimization (LTO)

LTO gives the compiler visibility across translation units, enabling optimizations that wouldn't happen without it. This can inline your measurement points away or reorder across them.

**If using LTO:** put your profiling point declarations in a separate translation unit, or use `__attribute__((noinline))` on the function being measured.

---

## 5. Caches, Flash Wait States, and Why Your Numbers Vary

If you measure the same function 1000 times and get 1000 different numbers, this section explains why.

### Instruction Cache (I-Cache)

Most Cortex-M7 (and some M4) cores have instruction caches. The first execution of a function fetches instructions from flash (slow, with wait states). Subsequent executions may hit the cache (fast, zero wait states).

**Consequence:** The first call is always slower. Sometimes much slower.

**What to do:**
- Discard the first N measurements (warm-up), or
- Measure both cold and warm performance separately — both are useful data
- embprof handles this naturally: the histogram shows you the distribution, and outliers from cold starts will appear as a tail

### Flash Wait States

Running at 160 MHz from flash that needs 5 wait states means each cache miss costs 5 extra cycles per fetch. ST's ART accelerator (128-bit prefetch + branch cache) mitigates this but doesn't eliminate it. Branch-heavy code suffers more.

**Consequence:** The same function can take different amounts of time depending on whether the flash prefetcher is warmed up and how many branches were taken.

### Data Cache (D-Cache)

Cortex-M7 has a data cache. Functions that access memory will run faster on subsequent calls if the data is still in cache.

**What to do:** If you need to measure worst-case performance, flush caches before measuring:

```c
// Cortex-M7: clean and invalidate D-cache
SCB_CleanInvalidateDCache();
// Cortex-M7: invalidate I-cache
SCB_InvalidateICache();
```

### Pipeline Effects

The Cortex-M4 has a 3-stage pipeline; M7 has 6 stages. Branch mispredictions, load-use hazards, and bus contention all introduce cycle-level variation.

**Bottom line:** Expect 5-15% variance even on "deterministic" code. This is normal. Use statistics (mean, stddev, percentiles) instead of single measurements.

---

## 6. Interrupts and Preemption

The #1 source of inflated measurements.

### The Problem

If an interrupt fires during your timed section, the ISR execution time gets added to your measurement. An interrupt that takes 200 cycles will add 200 cycles to whichever measurement it lands in.

### Strategies

**Strategy 1: Disable interrupts during measurement**

```c
uint32_t primask = __get_PRIMASK();
__disable_irq();

point.start();
my_function();
point.stop();

__set_PRIMASK(primask);  // restore previous state
```

**Pros:** Gives you clean measurements.
**Cons:** Increases interrupt latency. Unacceptable in many real-time systems. Not representative of actual execution conditions.

**Strategy 2: Measure with interrupts enabled, use statistics**

This is usually the right approach. Your function *does* run with interrupts enabled in production, so measurements with interrupts enabled are the real numbers. Use percentiles and histograms to separate the "clean" executions from the "interrupted" ones:

- **p50** represents the typical (uninterrupted) case
- **p99** represents the worst case including interrupt interference
- The histogram will show a clear bimodal distribution if interrupts are significantly impacting your code

**Strategy 3: Measure interrupt overhead separately**

Profile your ISRs independently. If you know the ISR takes 150 cycles and fires at 10 kHz, you can estimate its impact on any measurement.

### RTOS Considerations

If you're running under an RTOS (FreeRTOS, Zephyr, etc.), context switches add the same kind of overhead as interrupts. The PendSV and SysTick handlers add their own overhead.

For task-level profiling under an RTOS, measure wall time (what the task actually experiences) and optionally track how much of that time was spent in ISRs/scheduler. Some RTOSes (e.g., FreeRTOS with `configGENERATE_RUN_TIME_STATS`) have built-in mechanisms for this.

---

## 7. Counter Overflow and Wraparound

### DWT CYCCNT (32-bit)

At 160 MHz: wraps every **26.8 seconds**.

For function-level timing (<1 second), wraparound is not a concern. The subtraction `end - start` works correctly even across a single wrap because unsigned arithmetic wraps modulo 2^32:

```
// If start = 0xFFFFFFF0 and end = 0x00000010 (counter wrapped):
// end - start = 0x00000010 - 0xFFFFFFF0 = 0x00000020 = 32 cycles ✓
```

This only fails if the measured interval exceeds 2^32 cycles (~26.8s at 160MHz). If you're measuring something that long, use a hardware timer with a wider counter or implement a software extension (ISR on overflow).

### SysTick (24-bit)

At 160 MHz: wraps every **0.105 seconds**.

SysTick counts down. If the measured section exceeds the reload value, the subtraction is wrong. Keep measured sections short, or use DWT instead.

---

## 8. What to Measure

Choosing what to profile is more important than how you profile it.

### Start With the Hot Path

Profile the functions that run most frequently:
- Main control loop body
- ISR handlers (keep these short)
- Communication protocol handlers (CAN RX, UART RX)
- Filter/algorithm computations
- ADC conversion + processing chains

### Worst Case Execution Time (WCET)

For safety-critical or hard real-time systems, you care about the **maximum** execution time, not the average. embprof tracks `max` and `p99` for this purpose. But note:

- Measured WCET is a lower bound — you've only seen the worst case *so far*
- True WCET analysis requires static analysis tools (aiT, RapiTime, etc.)
- For most non-safety-critical embedded work, measured p99 over long runs is sufficient

### Don't Over-Instrument

Each profiling point costs ~700 bytes of RAM (with default 20-bucket histogram). On a 64KB RAM microcontroller, 32 profiling points consume ~22KB — over a third of your RAM.

For the lite header, use 1-3 profiling points at a time. Move them around as needed. You don't need to profile everything simultaneously.

---

## 9. Statistical Thinking

A single measurement tells you almost nothing. Distributions tell you everything.

### What the Numbers Mean

| Metric | What It Tells You |
|---|---|
| **count** | How many samples you've collected |
| **mean** | Average execution time — affected by outliers |
| **min** | Best case — often the I-cache-warm, no-interrupt case |
| **max** | Worst case *observed* — often an interrupted execution |
| **stddev** | How much variation there is — high stddev means unpredictable |
| **p50** | Median — the "typical" execution, robust to outliers |
| **p90** | 90% of executions are faster than this |
| **p99** | 99% of executions are faster than this — your practical WCET |

### How Many Samples Do You Need?

- **Mean/stddev stabilize** after ~100-500 samples
- **p99 needs at least 1000 samples** to be meaningful (you need 100 occurrences past the 99th percentile to have confidence)
- **p99.9 needs 10,000+**
- **For real WCET confidence:** run for hours under realistic load

### Interpreting Histograms

A healthy execution time histogram for a typical embedded function looks like:

```
              ┌─── Main peak: most executions land here
              │    (cache warm, no interrupts)
  count  ▐█████▌
         ▐█████▌
         ▐█████▌         ┌─── Long tail: interrupted executions
         ▐█████▌    ▐█▌  │    or cache-cold starts
         ▐█████▌    ▐█▌──┘
         ▐██████████████▌
         └─────────────────── time (ticks)
```

If you see a **bimodal distribution** (two peaks), common causes are:
- Cache cold vs. warm
- Branch taken vs. not taken in a major code path
- Interrupt landing in the measurement vs. not

If you see a **uniform distribution** (flat), something is wrong — likely your timer isn't working or the function has wildly data-dependent execution time.

---

## 10. Online vs. Offline Analysis

### Online (Running Approximations)

This is what embprof does by default. All statistics are computed incrementally:

- **Welford's algorithm** for mean/variance — O(1) memory, numerically stable
- **P² algorithm** for quantiles — 5 markers (~80 bytes), no observation storage
- **Fixed-bucket histogram** — predetermined boundaries, constant memory

**Advantages:** No storage of individual samples. Constant memory. Can run indefinitely.

**Disadvantages:** Histogram bucket boundaries must be chosen upfront. P² quantile estimates have ~1-5% error for well-behaved distributions.

### Offline (Store and Analyze Later)

Store raw samples in a buffer, dump them after the run, analyze on a PC.

**Advantages:** Exact statistics, flexible analysis, can compute anything post-hoc.

**Disadvantages:** RAM-hungry (4 bytes per sample × N samples), limited run duration, requires data transfer.

### Hybrid (What embprof Supports)

Use online statistics during the run for live monitoring. Periodically serialize the state (via the serialization API) and send it to the host for visualization or long-term tracking. The host can merge states from multiple runs.

---

## 11. Getting Data Off the Target

### ITM / SWO (Recommended for Development)

The Instrumentation Trace Macrocell sends data through the SWO pin to your debugger:

- **Overhead:** ~1 cycle per character written
- **Bandwidth:** Typically 64 kbaud default (configurable up to several Mbaud)
- **Tools:** J-Link SWO Viewer, winIDEA Terminal, OpenOCD SWO capture, Ozone
- **Ports:** 32 stimulus ports — use port 0 for printf-style text, others for structured data

embprof's `itm_sink<0>` writes directly to ITM port 0. Use `report_summary()` for a compact single-line output per profiling point.

For a complete walkthrough of setting up ITM output with iSYSTEM winIDEA and the BlueBox, see [WINIDEA_ITM_GUIDE.md](WINIDEA_ITM_GUIDE.md).

### UART

Classic approach. Use embprof's `callback_sink` with your UART TX function.

**Tip:** Use DMA-based UART TX to minimize the overhead of sending data. Construct the report into a buffer, then kick off a DMA transfer.

### USB CDC

Same as UART but often faster (12 Mbps Full Speed, 480 Mbps High Speed). Use `callback_sink` with your CDC write function.

### Serialization to Flash/NVRAM

Use `embprof::serialize()` to dump the profiling state to internal flash or EEPROM. Read it back on the next boot or via a debugger memory dump. The format is `[EPRF][v1][size][state]` — compact enough for CAN frames.

### CAN

If your system is on a CAN bus, you can send serialized profiling state as a sequence of CAN frames. Each frame carries 8 bytes (CAN 2.0B) or 64 bytes (CAN FD). A full serialized profiling point with 20 histogram buckets is ~400 bytes — that's 50 CAN 2.0B frames or 7 CAN FD frames.

---

## 12. Common Pitfalls

### Pitfall: Measuring in Debug Mode

Debug builds (`-O0`) have dramatically different performance characteristics:
- No inlining
- All variables on the stack
- No loop unrolling
- Every variable access is a load/store

**Always profile with the same optimization level you ship.** `-O2` or `-Os` minimum. Ideally use your exact release build with profiling points added.

### Pitfall: Measuring With the Debugger Halting the CPU

Some debugger operations (setting breakpoints, reading memory) halt the core. The DWT counter **stops** when the core is halted (it counts core clock cycles, not wall time). This means:
- Breakpoints don't inflate your measurements ✓
- But stepping through code gives you cycle-accurate per-instruction cost ✓
- However, live variable watches via the debugger *can* stall the bus and affect timing

### Pitfall: Confusing Ticks with Time

DWT counts CPU cycles, not microseconds. To convert:

```
microseconds = cycles / (SystemCoreClock / 1000000)
```

But be careful: if your MCU dynamically scales the clock (e.g., entering low-power mode), the conversion factor changes. Record the clock speed alongside your measurements.

### Pitfall: Memory-Mapped Peripheral Access in Measured Code

If your measured function accesses slow peripherals (external QSPI flash, SDRAM, peripheral registers behind APB bridges), those accesses add wait states that vary based on bus arbitration.

### Pitfall: Profiling Points in ISRs

ISRs must be fast. Adding a full `profiling_point::record()` (~50-100 cycles) to a high-frequency ISR may be acceptable for a 10 kHz ISR (100µs period vs. ~0.6µs overhead at 160 MHz) but not for a 1 MHz ISR.

For very fast ISRs, use raw DWT reads and a separate, simpler accumulator. Use the full profiling point only for ISRs where the overhead is <1% of the ISR period.

### Pitfall: Stack Overflow From Profiling Points

Each `profiling_point<20>` is ~700 bytes. If you declare one as a local variable inside a function that already has a tight stack budget, you'll overflow the stack silently (no exception on most Cortex-M without MPU).

**Always declare profiling points as `static` or global.** Never on the stack.

---

## 13. Recommended Workflow

### Phase 1: Establish Baseline

1. Add `dwt_init()` to your startup code
2. Add 1-3 profiling points to your main hot paths
3. Run for several minutes under realistic load
4. Record mean, stddev, p50, p99, and the histogram shape

### Phase 2: Investigate

5. If p99 >> p50, check for interrupt interference (temporarily disable interrupts to confirm)
6. If stddev is high, look at the histogram for multimodal distributions
7. If min is much less than mean, you might have a fast-path/slow-path branch worth optimizing

### Phase 3: Optimize

8. Make your change
9. Re-measure with the same profiling points
10. Compare the full distribution (not just the mean!) — an optimization that improves the mean but worsens p99 may not be acceptable in a real-time system

### Phase 4: Monitor

11. If the code is stable, use `report_summary()` over ITM for periodic health checks
12. Use serialization to snapshot state at intervals and compare across firmware versions
13. Set up alerting on p99 thresholds if you have a test harness

---

## 14. embprof Quick Reference

### Lite Header (Minimal, Drop-In)

```cpp
#include "embprof_lite.hpp"

// Declare once (static/global — never on the stack)
static embprof::profiling_point<> ctrl_loop("ctrl_loop", 100, 500000);

// RAII (preferred)
void control_loop() {
    embprof::scoped_timer<> t(ctrl_loop);
    // ... your code ...
}

// Manual start/stop
void other_function() {
    ctrl_loop.start();
    // ... your code ...
    ctrl_loop.stop();
}

// Read results
auto& s = ctrl_loop.stats();
float mean_us = s.mean() / (SystemCoreClock / 1e6f);
```

### Full Library (With ITM Output)

```cpp
#include <embprof/embprof.hpp>

static embprof::profiling_point<> ctrl_loop("ctrl_loop", 100, 500000);

// Every 1000 iterations, send summary over ITM
void periodic_report() {
    static uint32_t counter = 0;
    if (++counter % 1000 == 0) {
        embprof::itm_sink<0> itm;
        embprof::report_summary(itm, ctrl_loop);
    }
}

// Or use UART
void uart_report() {
    embprof::callback_sink uart([](const char* d, uint32_t n) {
        HAL_UART_Transmit(&huart2, (uint8_t*)d, n, 100);
    });
    embprof::report(uart, ctrl_loop);
}
```

### Choosing Histogram Bounds

The histogram lower and upper bounds should bracket your expected execution time range:

| Function Type | Suggested lo | Suggested hi | Bucket Mode |
|---|---|---|---|
| Fast ISR (1-10 µs) | 100 | 10,000 | log_linear |
| Control loop (10-100 µs) | 1,000 | 100,000 | log_linear |
| Communication handler | 500 | 500,000 | log_linear |
| Heavy computation (ms) | 10,000 | 10,000,000 | log_linear |

Use `log_linear` for most cases — execution time distributions are typically log-normal, so log-spaced buckets give you better resolution where the data actually is.

Values outside `[lo, hi)` are counted in underflow/overflow buckets, so you don't lose data — you just lose histogram resolution for those outliers.

---

## Further Reading

- ARM CoreSight Technical Reference Manual (DWT, ITM registers)
- "The Art of Electronics" Chapter 15 (Microcontrollers and Timing)
- Jain & Chlamtac, "The P-Square Algorithm for Dynamic Calculation of Percentiles and Histograms Without Storing Observations", CACM 1985
- Welford, "Note on a Method for Calculating Corrected Sums of Squares and Products", Technometrics 1962
