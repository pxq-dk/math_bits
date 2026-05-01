# math_bits

Header-only C++20 library for multiplying integers by a constant floating-point factor using integer bit-shifting — no FPU, no runtime division, compile-time unit tests.

---

## Features

- **No floating-point at runtime** — all FPU operations happen at compile time. The generated code is pure integer arithmetic.
- **Compile-time parameter generation** — multiplier, bit-shift count, and integer scale factor are all derived at compile time from the floating-point input.
- **Overflow safe** — the maximum multiplication factor is computed at compile time to guarantee no overflow for the given input range.
- **Configurable accuracy** — `max_error` template parameter sets the allowed deviation from the true floating-point result. Defaults to ±1 LSB.
- **Compile-time unit tests** — a `static_assert` runs a full test suite at compile time. A broken instantiation will not compile.
- **Header-only** — single `.h` file, no dependencies beyond the C++ standard library.
- **Inlining control** — optional `force_inlining` template parameter forces `[[gnu::always_inline]]` on the hot path.

---

## Requirements

- C++20 or later (`std::bit_width` is used for compile-time bit counting)
- Any C++20 compiler (GCC, Clang, MSVC)
- No hardware FPU required — designed for Cortex-M0/M0+ and other FPU-less targets

---

## Usage

### Basic

```cpp
#include "math_bits.h"

// Multiply uint16_t values by 0.75, input range [0, 1000], max error ±1
using scale75 = mult_bitshift<0.75, (uint16_t)1000, uint16_t, uint32_t>;

uint16_t result = scale75::mult(800);  // result ≈ 600
```

### Operator overload

```cpp
scale75 scaler;
uint16_t result = scaler * 800;  // same as scale75::mult(800)
```

### With forced inlining

```cpp
using scale75_inline = mult_bitshift<0.75, (uint16_t)1000, uint16_t, uint32_t, 1, true>;
uint16_t result = scale75_inline::mult(800);
```

---

## Template Parameters

| Parameter | Default | Description |
|---|---|---|
| `multvalue` | — | Floating-point multiplier (`float`, `double`, or `long double`) |
| `max_input_value` | — | Maximum input value the multiplier must handle without overflow |
| `io_type` | `uint32_t` | Input and output integer type. Must be unsigned. |
| `calc_type` | `uint32_t` | Internal calculation type. Must be unsigned and at least as wide as `io_type`. |
| `max_error` | `1` | Maximum allowed deviation from the true floating-point result (in LSB) |
| `force_inlining` | `false` | Force `[[gnu::always_inline]]` on the `mult()` function |

---

## API Reference

| Function | Description |
|---|---|
| `mult(input)` | Multiply `input` by the configured factor. Static — no instance needed. |
| `operator*(val)` | Instance operator overload — calls `mult(val)`. |
| `operator*(val, rhs)` | Friend operator overload — `val * scaler`. |

### Compile-time constants

| Constant | Description |
|---|---|
| `mult_factor` | The original floating-point multiplier |
| `max_input_int` | The configured maximum input value |
| `bitShifts` | Number of bits shifted in the integer multiplication |
| `mult_factor_int` | The integer scale factor derived from `mult_factor` |
| `max_deviation` | The configured `max_error` |

---

## Design Notes

**Why bit-shifting instead of floating-point?**
On Cortex-M0/M0+ there is no FPU. A floating-point multiply compiles to a software library call — slow, non-deterministic, and unsuitable for ISRs. By computing the scale factor at compile time and using a single integer multiply + shift at runtime, the hot path becomes 2–3 instructions with deterministic latency.

**Why compile-time unit tests?**
The test suite verifies that every value in a representative sample of the input range produces a result within `max_error` of the true floating-point result. If the chosen `max_error` is too tight for the given multiplier and types, the build fails with a clear message — no separate test binary required.

**Why waste one extra type parameter for `calc_type`?**
The intermediate product `input * mult_factor_int` can overflow `io_type`. Using a wider `calc_type` (e.g. `uint32_t` when `io_type` is `uint16_t`) keeps the intermediate value safe and shifts back down to `io_type` at the end.

---

## Performance (STM32G051, Cortex-M0+, `-Os`)

- **`mult()`**: 2–3 instructions — one multiply, one shift, one cast
- **No FPU instructions** — zero soft-float library calls at runtime
- **Compile-time overhead**: parameter generation and unit test run entirely at compile time — zero runtime cost

---

## License

Copyright (c) 2026 PxQ Technologies — https://pxq.dk

This software is dual-licensed:

**1. Open Source — GNU General Public License v3.0 (GPLv3):**
Free to use, modify, and distribute under the terms of the GNU General Public License version 3, as published by the Free Software Foundation. Note that GPLv3 is strong copyleft — derivative works and products that incorporate this software must also be released under GPLv3.

**2. Commercial License:**
For use in proprietary or closed-source products that cannot or do not wish to comply with the GPLv3, a commercial license is available from PxQ Technologies — either as a written agreement, or via direct delivery by Erik Nørskov as part of a paid engagement (in which case the license is granted for that specific project scope only).

Contact: https://pxq.dk
