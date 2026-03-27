# Using embprof with winIDEA ITM Terminal Output

This guide walks through setting up real-time profiling output from embprof to the winIDEA Terminal window via the ARM ITM (Instrumentation Trace Macrocell) and SWO (Single Wire Output) trace.

The result: profiling summaries, histograms, and statistics streaming live to your winIDEA Terminal while the target runs at full speed with negligible overhead.

---

## Table of Contents

1. [Prerequisites](#1-prerequisites)
2. [How It Works](#2-how-it-works)
3. [Target-Side Setup](#3-target-side-setup)
4. [winIDEA Configuration](#4-winidea-configuration)
5. [Using embprof with the ITM Sink](#5-using-embprof-with-the-itm-sink)
6. [Using Multiple Stimulus Ports](#6-using-multiple-stimulus-ports)
7. [Viewing Output in the Trace Window](#7-viewing-output-in-the-trace-window)
8. [Troubleshooting](#8-troubleshooting)
9. [Performance Considerations](#9-performance-considerations)
10. [Complete Example](#10-complete-example)

---

## 1. Prerequisites

### Hardware

- **iSYSTEM BlueBox** debug probe with a valid trace license
- Target board with an **ARM Cortex-M3, M4, or M7** SoC (Cortex-M0/M0+ do not have the ITM)
- **SWO pin** routed from the SoC to the debug connector (typically pin 6 on a standard 10-pin Cortex debug header, shared with JTAG TDO)

### Software

- **winIDEA** (any recent version with Terminal window support)
- A working debug session — the SoC must be selected under `Debug | Configure Session | SoCs` and you must be able to download and run firmware before attempting trace

### Target Firmware

- CMSIS Core headers available (for `core_cm4.h` or equivalent), or use embprof's raw register writes (no CMSIS dependency required)
- embprof library (full library via `#include <embprof/embprof.hpp>` or the lite header)

---

## 2. How It Works

```
┌─────────────┐       ITM Stimulus        ┌────────┐    SWO pin    ┌─────────┐
│  Your Code  │──write to PORT[n]──────────│  TPIU  │──────────────│ BlueBox │
│  (embprof)  │   (1 cycle per byte)       │        │  (async NRZ) │         │
└─────────────┘                            └────────┘              └────┬────┘
                                                                       │
                                                                       │ USB
                                                                       ▼
                                                                 ┌───────────┐
                                                                 │  winIDEA  │
                                                                 │ Terminal  │
                                                                 └───────────┘
```

1. Your firmware writes characters to the ITM stimulus port registers (`0xE0000000 + 4*port`)
2. The ITM packetizes these writes into trace packets
3. The TPIU (Trace Port Interface Unit) serializes the packets over the SWO pin
4. The BlueBox captures the SWO stream and forwards it to winIDEA
5. winIDEA decodes the packets and displays the text in the Terminal window

embprof's `itm_sink<Port>` handles step 1. The rest is hardware + winIDEA configuration.

---

## 3. Target-Side Setup

### Option A: Let the debugger handle ITM initialization (recommended)

winIDEA and the BlueBox can configure the ITM hardware registers automatically when you set up trace. In this case, you do not need any ITM initialization code in your firmware — just write to the stimulus port registers and winIDEA takes care of enabling ITM, TER, and the SWO clock.

This is the simplest approach and is recommended for development.

### Option B: Initialize ITM from firmware

If you want the ITM output to work without a debugger connected (e.g., capturing SWO with a logic analyzer or a dedicated SWO-to-UART bridge), initialize the ITM yourself:

```c
#include "core_cm4.h"  // or core_cm7.h — provides ITM, CoreDebug, TPIU structs

/// Initialize ITM for SWO output.
/// @param cpu_freq_hz   Core clock frequency (e.g., 160000000 for 160 MHz)
/// @param swo_baud      Desired SWO baud rate (e.g., 2000000 for 2 Mbaud)
/// @param port_mask     Bitmask of stimulus ports to enable (e.g., 0x1 for port 0)
void itm_init(uint32_t cpu_freq_hz, uint32_t swo_baud, uint32_t port_mask) {
    // Enable trace in the Debug Exception and Monitor Control Register
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

    // Configure TPIU for async SWO (NRZ encoding)
    TPI->SPPR  = 2;  // NRZ (UART-like) encoding
    TPI->ACPR  = (cpu_freq_hz / swo_baud) - 1;  // Baud rate prescaler

    // Unlock ITM and configure
    ITM->LAR   = 0xC5ACCE55;  // Unlock access
    ITM->TCR   = (1 << 0)     // ITMENA: Enable ITM
               | (1 << 3)     // DWTENA: Enable DWT stimulus
               | (1 << 16);   // TraceBusID = 1
    ITM->TPR   = 0;           // All ports accessible from unprivileged code
    ITM->TER   = port_mask;   // Enable selected stimulus ports

    // Enable DWT cycle counter while we're here (for profiling)
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}
```

Call this once at startup:

```c
int main(void) {
    // System init, clock config, etc.
    SystemInit();
    SystemCoreClockUpdate();

    // Enable ITM on port 0, 2 Mbaud SWO
    itm_init(SystemCoreClock, 2000000, 0x1);

    // ... rest of your application
}
```

**Important:** If winIDEA is also configuring the ITM (which it does when trace is enabled), the debugger's configuration may overwrite yours on connect. This is usually fine — it means the ITM works regardless of whether your firmware init ran. But be aware that the SWO baud rate winIDEA uses may differ from what you configured.

### Without CMSIS headers

embprof's `itm_sink<Port>` uses raw register addresses and works without any CMSIS headers. If you prefer not to depend on `core_cm4.h`, here's the equivalent init using raw addresses:

```c
void itm_init_raw(uint32_t cpu_freq_hz, uint32_t swo_baud, uint32_t port_mask) {
    // DEMCR: enable trace
    *((volatile uint32_t*)0xE000EDFC) |= (1 << 24);

    // TPIU: NRZ encoding
    *((volatile uint32_t*)0xE0040004) = 2;
    // TPIU: async clock prescaler
    *((volatile uint32_t*)0xE0040010) = (cpu_freq_hz / swo_baud) - 1;

    // ITM Lock Access Register: unlock
    *((volatile uint32_t*)0xE0000FB0) = 0xC5ACCE55;
    // ITM Trace Control Register
    *((volatile uint32_t*)0xE0000E80) = (1 << 0) | (1 << 3) | (1 << 16);
    // ITM Trace Privilege Register
    *((volatile uint32_t*)0xE0000E40) = 0;
    // ITM Trace Enable Register
    *((volatile uint32_t*)0xE0000E00) = port_mask;

    // DWT: enable cycle counter
    *((volatile uint32_t*)0xE0001004) = 0;
    *((volatile uint32_t*)0xE0001000) |= 1;
}
```

---

## 4. winIDEA Configuration

### Step 1: Select Debug Protocol

Open **Hardware | CPU Options | SoC**.

- Set **Debug Protocol** to **SWD**
  - SWO trace requires SWD — it cannot be used with JTAG because the SWO pin is shared with the JTAG TDO signal
- Under the **Trace** section, set **Trace Capture method** to **SWO**

If your board routes parallel trace pins (TRACECLK + TRACEDATA[0:3]), you can alternatively select **Parallel** trace, which provides higher bandwidth. In that case, perform a **Trace Line Calibration** as prompted by winIDEA.

### Step 2: Configure ITM Stimulus Ports (Optional)

This step configures which ITM ports winIDEA records in the Trace window. The Terminal window has its own filter (Step 3), but if you also want the raw trace data visible in the Analyzer:

Open **View | Analyzer | Analyzer Configuration | Manual Hardware Trigger | Configure | ITM**.

- Under **Record Stimuli**, enable the ports you plan to use (e.g., port 0)
- Under **Timestamps**, configure:
  - For **SWO**: enable **Local** timestamps, source **ASync**, prescaler as needed
  - For **Parallel**: leave Local timestamps disabled (winIDEA uses the debugger's internal timestamp)

### Step 3: Configure the Terminal Window

This is where the actual text output will appear.

1. Open **View | Workspace | Terminal** (or click the Terminal button in the Debug Toolbar)
2. Right-click inside the Terminal window and select **Options**
3. Click **Configure**
4. Set **Communication type** to **Trace Channel**
5. Click **Advanced**
6. Under **ITM Stimuli**, select:
   - **All** to show output from all 32 stimulus ports, or
   - **Selected** and check the specific ports you want (e.g., **ITM (0)**)

### Step 4: Connect and Run

1. Perform a **Debug Download** (download your firmware to the target)
2. In the Terminal window, click the **Connect** button (plug icon)
3. **Run** the application (F5 or the Run button)
4. Output from `ITM_SendChar` / embprof's `itm_sink` will appear in the Terminal window in real time

---

## 5. Using embprof with the ITM Sink

### Basic: Periodic Summary to ITM Port 0

```cpp
#include <embprof/embprof.hpp>

// Profiling point — declared static (never on the stack)
static embprof::profiling_point<20> ctrl_loop(
    "ctrl_loop", 100, 500000, embprof::bucket_mode::log_linear);

// Call this periodically (e.g., every 1000 iterations)
void send_profiling_summary() {
    embprof::itm_sink<0> itm;
    embprof::report_summary(itm, ctrl_loop);
    // Output: [ctrl_loop] n=1000 mean=1234.56 p50=1200.34 p90=2345.67 p99=8765.43
}

void main_loop() {
    static uint32_t iter = 0;
    while (true) {
        {
            embprof::scoped_timer<> t(ctrl_loop);
            // ... your control loop code ...
        }
        if (++iter % 1000 == 0) {
            send_profiling_summary();
        }
    }
}
```

### Full Report with Histogram

```cpp
void send_full_report() {
    embprof::itm_sink<0> itm;
    embprof::report(itm, ctrl_loop);
    // Outputs multi-line report: stats, quantiles, and histogram
}
```

This produces something like:

```
[ctrl_loop]
  count : 1000
  mean  : 1234.56
  min   : 800.00
  max   : 9500.00
  stddev: 456.78
  p50   : 1150.00
  p90   : 2100.00
  p99   : 7800.00
  --- histogram (20 buckets) ---
        <100.00: 0
  [100.00, 141.25): 2
  [141.25, 199.53): 15
  ...
       >=500000.00: 0
```

### Report All Registered Points

If you use the full library with the registry:

```cpp
#include <embprof/embprof.hpp>

static embprof::profiling_point<20> ctrl_loop("ctrl_loop", 100, 500000);
static embprof::profiling_point<20> can_handler("can_handler", 50, 50000);

// Register both
static embprof::registered_point<20> reg_ctrl(ctrl_loop);
static embprof::registered_point<20> reg_can(can_handler);

void report_all_to_itm() {
    embprof::itm_sink<0> itm;
    embprof::report_all(itm);  // Dumps stats for every registered point
}
```

---

## 6. Using Multiple Stimulus Ports

The ITM provides 32 stimulus ports. You can use different ports to separate different types of output:

| Port | Suggested Use |
|------|---------------|
| 0 | Text output (printf-style, profiling summaries) |
| 1 | Structured data (binary profiling state via `serialize()`) |
| 2 | Event markers (function entry/exit timestamps) |
| 3-31 | Application-specific |

```cpp
// Text report on port 0
embprof::itm_sink<0> text_out;
embprof::report_summary(text_out, ctrl_loop);

// Binary state dump on port 1
// (won't display nicely in Terminal, but you can capture the raw trace)
embprof::itm_sink<1> binary_out;
constexpr auto sz = embprof::serialized_size<20, embprof::default_clock>();
uint8_t buf[sz];
uint32_t written = embprof::serialize(ctrl_loop, buf, sz);
binary_out.write(reinterpret_cast<const char*>(buf), written);
```

In winIDEA's Terminal, configure it to show only port 0 (for readable text). Use the **Trace window** (View | Analyzer | Trace) to inspect raw data on all ports — the stimulus port ID appears in the Address column.

---

## 7. Viewing Output in the Trace Window

In addition to the Terminal, winIDEA can show ITM writes in the **Trace window**:

1. Open **View | Analyzer | Trace**
2. Each ITM write appears as a row:
   - **Address column**: stimulus port number (e.g., `ITM_0`)
   - **Data column**: the byte(s) written
   - **Timestamp column**: when the write occurred (if timestamps are enabled)

This is useful for:
- Verifying that ITM writes are actually reaching the BlueBox
- Inspecting binary data sent on non-text ports
- Correlating profiling output with program execution flow
- Debugging issues where the Terminal shows nothing

### Combining with Function/Data Profiling

winIDEA's Analyzer can simultaneously capture:
- ITM software trace (your profiling output)
- DWT data trace (hardware watchpoint triggers)
- ETM instruction trace (if parallel trace is available)

This means you can correlate embprof's execution time measurements with a full instruction-level trace of what the CPU was actually doing — very powerful for root-causing timing anomalies.

---

## 8. Troubleshooting

### Terminal shows nothing

| Check | How |
|-------|-----|
| **SWO pin connected?** | Verify the SWO/TDO pin is routed from the SoC to pin 6 of the debug connector. Some custom boards omit this trace. |
| **SWD mode selected?** | SWO only works with SWD, not JTAG. Check Hardware \| CPU Options \| SoC. |
| **Trace capture set to SWO?** | Check Hardware \| CPU Options \| SoC \| Trace section. |
| **Terminal Communication = Trace Channel?** | Right-click Terminal \| Options \| Configure. Must be "Trace Channel", not COM port. |
| **Correct stimulus port selected?** | In Terminal \| Options \| Configure \| Advanced, ensure the port matches what your code writes to (e.g., ITM (0)). |
| **Terminal connected?** | Click the Connect icon in the Terminal toolbar. The status bar should show "Connected". |
| **Application running?** | The target must be running (not halted). ITM data is only produced when the CPU executes your code. |
| **ITM enabled on the target?** | If using Option A (debugger-managed ITM), winIDEA should handle this. If using Option B, verify your init code ran. Check in the register view: ITM->TCR bit 0 should be 1, ITM->TER should have your port bit set. |

### Garbled or missing characters

- **SWO baud rate mismatch**: The BlueBox auto-detects the SWO baud rate in most cases. If you manually configured the TPIU prescaler in firmware, ensure it matches what winIDEA expects. Try removing your firmware's TPIU configuration and letting winIDEA handle it.
- **SWO bandwidth exceeded**: At 64 kbaud (default), you can send ~8 KB/s. A full report with 20 histogram buckets is ~500 bytes. Sending it every 100 ms would be 5 KB/s — fine. But sending it every 10 ms at 64 kbaud will overflow the SWO FIFO. Either increase the SWO speed or reduce output frequency.
- **Writing u16/u32 instead of u8**: embprof's `itm_sink` writes `PORT[n].u8` which is correct. If you're mixing with other ITM code that writes u16 or u32, the Terminal may misinterpret the data. Stick to u8 writes for text.

### Trace window shows ITM data but Terminal doesn't

The Terminal and Trace windows have independent stimulus port filters. Verify the Terminal's Advanced settings match the port you're writing to.

---

## 9. Performance Considerations

### Overhead of ITM Writes

Each byte written to an ITM stimulus port costs:
- 1 cycle for the register write itself
- 0-N cycles waiting for the port to be ready (FIFO not full)

Under normal conditions with a fast SWO baud rate (2+ Mbaud), the FIFO rarely fills, and writes are essentially free (1 cycle each).

### When the SWO FIFO Fills Up

If you write faster than the SWO link can drain, `itm_sink` busy-waits until the port is ready (with a 100k-iteration timeout to prevent infinite hangs). This stall directly impacts your measured execution time.

**Mitigation strategies:**

1. **Increase SWO baud rate** — The maximum depends on your SoC and debug probe. Many NXP S32K3 parts support 2-4 Mbaud. The BlueBox supports high SWO speeds.
2. **Reduce output frequency** — Send summaries every 1000-10000 iterations, not every iteration.
3. **Use `report_summary()` instead of `report()`** — A summary line is ~80 bytes. A full report with histogram is ~500+ bytes.
4. **Don't send output inside the timed section** — Always separate the timing measurement from the reporting:

```cpp
// GOOD: measurement and reporting are separate
{
    embprof::scoped_timer<> t(ctrl_loop);
    do_work();
}
// Report outside the timed block — ITM stalls don't pollute measurements
if (should_report()) {
    embprof::itm_sink<0> itm;
    embprof::report_summary(itm, ctrl_loop);
}

// BAD: reporting happens inside the timed section
{
    embprof::scoped_timer<> t(ctrl_loop);
    do_work();
    embprof::itm_sink<0> itm;
    embprof::report_summary(itm, ctrl_loop);  // This pollutes the measurement!
}
```

### Memory Footprint

`itm_sink` is a stateless static struct — zero RAM cost. All functions are `static`, so no object allocation is needed. The template parameter `Port` is a compile-time constant.

---

## 10. Complete Example

A complete firmware snippet for an NXP S32K3-based project using embprof with ITM output via winIDEA:

```cpp
#include <embprof/embprof.hpp>

// -- Profiling points (static, never on stack) --
static embprof::profiling_point<20> motor_ctrl(
    "motor_ctrl", 500, 200000, embprof::bucket_mode::log_linear);
static embprof::profiling_point<10> can_rx(
    "can_rx", 100, 50000, embprof::bucket_mode::log_linear);

// -- Register for enumeration --
static embprof::registered_point<20> reg_motor(motor_ctrl);
static embprof::registered_point<10> reg_can(can_rx);

// -- DWT init (call once) --
void dwt_init() {
    // Enable trace block
    *((volatile uint32_t*)0xE000EDFC) |= (1u << 24);
    // Reset and enable cycle counter
    *((volatile uint32_t*)0xE0001004) = 0;
    *((volatile uint32_t*)0xE0001000) |= 1u;
}

// -- Motor control ISR or periodic task --
void motor_control_handler() {
    embprof::scoped_timer<20> t(motor_ctrl);

    // ... FOC algorithm, PWM update, etc. ...
}

// -- CAN RX handler --
void can_rx_handler(/* CAN frame */) {
    embprof::scoped_timer<10> t(can_rx);

    // ... decode CAN frame, update state ...
}

// -- Background task: periodic reporting --
void background_1hz_task() {
    embprof::itm_sink<0> itm;

    // Compact one-liner per point — fits nicely in winIDEA Terminal
    embprof::report_summary(itm, motor_ctrl);
    embprof::report_summary(itm, can_rx);
}

int main() {
    system_init();
    dwt_init();

    // ... setup peripherals, start RTOS tasks, etc. ...

    while (true) {
        // main loop or RTOS idle
    }
}
```

In winIDEA, configure SWD + SWO, open the Terminal, set it to Trace Channel with ITM (0), connect, and run. You'll see output like:

```
[motor_ctrl] n=10000 mean=3456.78 p50=3200.10 p90=4500.33 p99=12300.56
[can_rx] n=523 mean=890.12 p50=850.44 p90=1200.67 p99=3400.89
[motor_ctrl] n=20000 mean=3460.11 p50=3205.22 p90=4510.00 p99=12250.78
[can_rx] n=1047 mean=888.90 p50=849.11 p90=1198.33 p99=3395.22
```

Updating live, once per second, while the motor control loop runs at full speed.

---

## References

- [iSYSTEM: Configure Cortex-M ITM Trace for printf() Debugging](https://www.isystem.com/downloads/winIDEA/help/how-to-itm-trace-terminal-window.html)
- [iSYSTEM: ITM Module Overview](https://www.isystem.com/downloads/winIDEA/help/cortex-analyzer-itm.html)
- [iSYSTEM: Terminal Window Documentation](https://www.isystem.com/downloads/winIDEA/help/terminalwindow.html)
- [iSYSTEM: Cortex-M Trace Configuration](https://www.isystem.com/downloads/winIDEA/help/tutorial-trace-arm-cortex-specific-settings.html)
- [MCU on Eclipse: SWO with ARM Cortex-M](https://mcuoneclipse.com/2016/10/17/tutorial-using-single-wire-output-swo-with-arm-cortex-m-and-eclipse/)
- [ARM: DWT Cycle Counter](https://developer.arm.com/documentation/ddi0403/d/Debug-Architecture/ARMv7-M-Debug/The-Data-Watchpoint-and-Trace-unit/CYCCNT-cycle-counter-and-related-timers)
