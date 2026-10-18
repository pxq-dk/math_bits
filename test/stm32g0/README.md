# math_bits on-target tests (STM32G0)

On-target tests for `math_bits.h`, used to verify `mult_bitshift_ratio_dyn` on an STM32G071 (Cortex-M0+, 64 MHz). Kept as they were run on 2026-10-11, not yet turned into a general module. Nothing here is compiled unless a `.cpp` includes it: the `.inc` extension keeps STM32CubeIDE from building the files.

| File | What |
|---|---|
| `math_bits_hwtest.inc` | Functional and concurrency stress test (`mbt::run()`). |
| `math_bits_bench.inc` | Cycle benchmark: static vs runtime gain vs plain C division (`mbb::run()`). |
| `hwtest_decode.py`, `bench_decode.py` | Decode the result arrays read from the target. |

## Running

1. In a test build of the project, include one of the files in `main_cpp.cpp` (after `#include "math_bits.h"`) and call `mbt::run()` or `mbb::run()` after the HAL, clock and SysTick init. Both never return.
2. Flash, let it run, then read the result array (`mbt::r` or `mbb::r`; the address is in the map file or `arm-none-eabi-nm -C <elf> | grep '::r$'`) over SWD as a hex dump.
3. Decode it: `python hwtest_decode.py < dump.txt` or `python bench_decode.py dump.txt`.

**`math_bits_hwtest.inc`**: `r[DONE]` becomes 1 when finished; the run time per stress scenario is `stress_ms` (20 s; 300 s for a soak). It uses TIM6 and TIM7 and defines their interrupt handlers (`TIM6_DAC_LPTIM1_IRQHandler`, `TIM7_LPTIM2_IRQHandler`), so the project must not use them, and `HAL_GetTick()` for timing.

- **Functional:** static double and ratio scalers against the target's own soft-float or exact integer reference; runtime gains (32-bit and 64-bit multiplier, rounding, `MPSC`, clamping, random gains) against exact `x·num/den`, including `set()` status codes; input and output clamping (`clamp_input`, `clamp_output`, cfg 26–28) and the fixed denominator (`set(num)`, cfg 29–30) over the whole `io_type` range, also above `max_input_value`.
- **Stress:** a jittered writer interrupt (TIM6, priority 2) and reader interrupt (TIM7, priority 1) change the gain and call `mult()` while main does the other side. The writer toggles between two gains; every output must equal the exact result of one of them (a torn multiplier gives neither). This covers both directions, 64-bit and 32-bit multipliers, two writers, and `prepare()` + `apply_new()`.
- **Negative control:** scenarios 0 and 1 run the same race on an unprotected 64-bit variable. They must show torn reads; zero would mean the test cannot detect tearing.

**`math_bits_bench.inc`**: `r[1]` becomes 1 when finished. Interrupts are off during the run; SysTick is reprogrammed as a free-running cycle counter. Each case is a `noinline` function called through a pointer over 256 random inputs with a warm cache; subtract the identity function (`base`) to get the net cost. `bench_decode.py` prints both raw and net figures. The fixed-denominator cases (`fA0`, `setF1`, `setF0`: gain `num/4095`, `set(num)` without a division) were added on 2026-10-18 and are not yet in the results below.

## Results 2026-10-11 (STM32G071, 64 MHz, 2 wait states, prefetch + I-cache)

**Stress test**, library scenarios 2–7 / negative-control scenarios 0–1:

| Build | Checks under stress | Errors | Control: torn reads |
|---|---|---|---|
| `-O0`, 20 s per scenario | ≈ 9.7 M | 0 | 21,133 / 96,870 |
| `-Os`, 20 s per scenario | ≈ 17.9 M | 0 | 7,265 / 29,416 |
| `-Os`, 300 s per scenario | ≈ 268 M | 0 | 109,571 / 437,792 |
| `-O0`, 300 s per scenario | ≈ 162 M | 0 | 318,517 / 1,458,399 |
| `-Os`, 20 s per scenario, 2026-10-18 (with clamp and fixed-denominator checks) | ≈ 16.7 M | 0 | 7,402 / 24,083 |

Functional part, every run: 115,840 output checks and 1,800 status checks, 0 failures. Since 2026-10-18 (cfg 26–30 added): 155,840 output checks and 2,300 status checks, 0 failures.

**Benchmark**, net cycles per call at `-Os`; average (min–max) where the cost depends on the input:

| Case | Static | Runtime gain | Plain C, constant | Plain C, runtime |
|---|---|---|---|---|
| 12-bit ADC → mV (3300/4095), `max_error 1` | 7 | 10 | 111 (45–117) | 113 (47–119) |
| 12-bit ADC → mV, exact | 7 (u32 calc) | 21 (u64 calc) | | |
| 24-bit µs → ms (1/1000), exact | 47 | 47 (`None`), 74 (`SPSC`) | 122 (37–134) | 662 (203–704) |

Runtime gain changes: `set()` ≈ 750–790 cycles (≈ 12 µs, mostly `__aeabi_uldivmod`), `prepare()` ≈ 830, `apply_new()` 43.

**Benchmark 2026-10-18** (same board and clock, `-Os`, image built from the project's startup/HAL with the bench as `main_cpp()`; 0 mismatches): fixed denominator `num/4095`, `set(num)` without a division: **60 cycles** (`max_error 1`, `u32` calc) and **64 cycles** (exact, `u64` calc), constant over all inputs — versus 747 / 777 (avg) for `set(num, den)` with a range of denominators in the same run. `mult()` of the fixed-denominator exact scaler: 23 cycles, the same as the runtime gain with a denominator range (`dA0`). The `mult()` figures of this build are 0–2 cycles above the 2026-10-11 table (static ADC 9, runtime 12 / 23, timer 49 / 76 / 49); code layout differs between the two builds. At `-O0`: `mult()` 34 (static ADC) to 188 cycles (`SPSC` timer), `set()` ≈ 1,800.
