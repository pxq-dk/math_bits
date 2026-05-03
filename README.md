# math_bits

Header-only C++20 library for multiplying integers by a constant floating-point factor using integer bit-shifting — no FPU, no runtime division, compile-time unit tests.

---

## Features

- **No floating-point at runtime** — all FPU operations happen at compile time. The generated code is pure integer arithmetic.
- **Compile-time parameter generation** — multiplier, bit-shift count, and integer scale factor are all derived at compile time from the floating-point input.
- **Overflow safe** — the maximum multiplication factor is computed at compile time to guarantee no overflow for the given input range.
- **Configurable accuracy** — `max_error` (in the options traits class) sets the allowed deviation from the true floating-point result. Defaults to ±1 LSB.
- **Compile-time unit tests** — a `static_assert` runs a full test suite at compile time. A broken instantiation will not compile.
- **Header-only** — single `.h` file, no dependencies beyond the C++ standard library.
- **Inlining control** — optional `force_inlining` flag (in the options traits class) forces `[[gnu::always_inline]]` on the hot path.

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

// Multiply uint16_t values by 0.75, input range [0, 1000], default options
using scale75 = mult_bitshift<0.75, (uint16_t)1000, uint16_t, uint32_t>;

uint16_t result = scale75::mult(800);  // result ≈ 600
```

### Operator overload

```cpp
scale75 scaler;
uint16_t result = scaler * 800;  // same as scale75::mult(800)
```

### Customizing options

The optional flags (`max_error`, `force_inlining`, `deep_test`, `clamp_input`) live in a traits-class struct. Derive from `mult_bitshift_options` and override only what you want — everything else stays at its default.

```cpp
struct fast_safe : mult_bitshift_options {
    static constexpr bool deep_test      = false;   // skip deep compile-time sweep
    static constexpr bool force_inlining = true;    // inline the hot path
    static constexpr bool clamp_input    = true;    // clamp inputs > max_input_value
};
using scale75_safe = mult_bitshift<0.75, (uint16_t)1000, uint16_t, uint32_t, fast_safe>;

uint16_t result = scale75_safe::mult(2000); // returns mult(1000), not garbage
```

Option structs compose — derive from another option struct to extend it:

```cpp
struct fast : mult_bitshift_options {
    static constexpr bool deep_test      = false;
    static constexpr bool force_inlining = true;
};
struct fast_with_clamp : fast {
    static constexpr bool clamp_input = true;
};
```

### Backwards-compatible legacy form

The previous positional-argument signature is preserved as `mult_bitshift_legacy`. Existing call sites can keep working by renaming `mult_bitshift` → `mult_bitshift_legacy`:

```cpp
// Same configuration as scale75_safe above, in the old positional form.
using scale75_safe_legacy =
    mult_bitshift_legacy<0.75, (uint16_t)1000, uint16_t, uint32_t,
                         /*max_error*/1, /*force_inlining*/true,
                         /*deep_test*/false, /*clamp_input*/true>;
```

New code should prefer the traits-class form — the legacy form is kept only to avoid breaking existing instantiations.

---

## Template Parameters

`mult_bitshift` takes five template parameters: two required values, two type parameters with defaults, and a traits-class type carrying the optional flags.

| Parameter | Default | Description |
|---|---|---|
| `multvalue` | — | Floating-point multiplier (`float`, `double`, or `long double`) |
| `max_input_value` | — | Maximum input value the multiplier must handle without overflow |
| `io_type` | `uint32_t` | Input and output integer type. Must be unsigned. |
| `calc_type` | `uint32_t` | Internal calculation type. Must be unsigned and at least as wide as `io_type`. |
| `Options` | `mult_bitshift_options` | Traits-class type carrying the optional flags below. Derive from `mult_bitshift_options` and override only the members you want. |

### `mult_bitshift_options` members

All members are `static constexpr`. Override only the ones you want by deriving a new struct:

| Member | Type | Default | Description |
|---|---|---|---|
| `max_error` | `uint64_t` | `1` | Maximum allowed deviation from the true floating-point result (in LSB). Generalized to `uint64_t` so the struct doesn't depend on `io_type`; the class casts back to `io_type` internally. Must fit in `io_type`. |
| `force_inlining` | `bool` | `false` | Force `[[gnu::always_inline]]` on the `mult()` function. |
| `deep_test` | `bool` | `true` | Run the full compile-time test sweep (up to 65535 inputs). Set `false` for a quick smoke test (100 inputs) when compile time matters. |
| `clamp_input` | `bool` | `false` | If `true`, clamp inputs above `max_input_value` to `max_input_value` before multiplying — guarantees output stays within the `max_input_value * mult_factor` envelope. Adds ~5 instructions on the hot path. When `false`, the clamp disappears entirely (zero cost). |

### Legacy positional form: `mult_bitshift_legacy`

For backwards compatibility, the previous positional signature is preserved as a separate alias. Identical behavior to the traits-class form — pick whichever style suits the call site:

| Position | Parameter | Default |
|---|---|---|
| 1 | `multvalue` | — |
| 2 | `max_input_value` | — |
| 3 | `io_type` | `uint32_t` |
| 4 | `calc_type` | `uint32_t` |
| 5 | `max_error` | `1` |
| 6 | `force_inlining` | `false` |
| 7 | `deep_test` | `true` |
| 8 | `clamp_input` | `false` |

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
| `max_output_int` | Precomputed `mult(max_input_int)` — the largest value `mult()` will ever return |
| `max_error` | The configured `max_error` from `Options`, cast to `io_type` |
| `max_deviation` | Same as `max_error` — kept for backwards compatibility |
| `force_inlining` | The configured `force_inlining` flag from `Options` |
| `deep_test` | The configured `deep_test` flag from `Options` |
| `clamp_input` | The configured `clamp_input` flag from `Options` |
| `inlined` | Alias for `force_inlining` (used internally to select the inline/non-inline `mult()` path) |
| `options` | The `Options` traits-class type itself, exposed for inspection |

---

## Design Notes

**Why bit-shifting instead of floating-point?**
On Cortex-M0/M0+ there is no FPU. A floating-point multiply compiles to a software library call — slow, non-deterministic, and unsuitable for ISRs. By computing the scale factor at compile time and using a single integer multiply + shift at runtime, the hot path becomes 2–3 instructions with deterministic latency.

**Why compile-time unit tests?**
The test suite verifies that every value in a representative sample of the input range produces a result within `max_error` of the true floating-point result. If the chosen `max_error` is too tight for the given multiplier and types, the build fails with a clear message — no separate test binary required. The sweep is bounded at 65535 samples; if compile time becomes a concern, set `deep_test=false` to drop to a 100-sample smoke test.

**Why waste one extra type parameter for `calc_type`?**
The intermediate product `input * mult_factor_int` can overflow `io_type`. Using a wider `calc_type` (e.g. `uint32_t` when `io_type` is `uint16_t`) keeps the intermediate value safe and shifts back down to `io_type` at the end.

---

## Performance (STM32G051, Cortex-M0+, `-Os`)

Verified by inspecting `arm-none-eabi-g++` output for representative configurations:

- **`mult()` with `calc_type ≤ uint32_t`**: hot path is `muls + lsrs` — one integer multiply, one bit-shift. A 1–2 instruction `movs/lsls` constant-load preamble brings the total to ~4 instructions on Cortex-M0+ (the constant load is an M0+ immediate-encoding limitation, not a library limitation).
- **`mult()` with `calc_type = uint64_t`**: the multiply is widened, so the compiler emits a call to the integer runtime helper `__aeabi_lmul` — still no FPU, still deterministic, but no longer a single instruction. **Prefer `calc_type=uint32_t` on Cortex-M0+ when your inputs and multiplier allow it.**
- **No FPU instructions** in either case — zero soft-float library calls at runtime.
- **Compile-time overhead**: parameter generation and unit test run entirely at compile time — zero runtime cost.
- **`clamp_input=true` + `force_inlining=true`** is the recommended combination when the clamp is needed. The clamp uses an early-return path with a precomputed `max_output_int`, which is slightly larger than the no-clamp body and may exceed GCC's `-Os` auto-inline threshold without `force_inlining=true`. With `force_inlining=true`, the clamp path is fully inlined into the caller (~9 instructions on the common path).
- **`[[gnu::flatten]]` on user code** is the strongest way to force inlining at a specific call site without touching the library — useful when calling `mult()` from a hot loop where you want every call inlined regardless of the library's `force_inlining` setting.

---

## License

Copyright (c) 2026 Erik Nørskov / PxQ Technologies — https://pxq.dk

This software is dual-licensed:

**1. Open Source — GNU General Public License v3.0 (GPLv3):**
Free to use, modify, and distribute under the terms of the GNU General Public License version 3, as published by the Free Software Foundation. Note that GPLv3 is strong copyleft — derivative works and products that incorporate this software must also be released under GPLv3.

**2. Commercial License:**
For use in proprietary or closed-source products that cannot or do not wish to comply with the GPLv3, a commercial license is available from PxQ Technologies — either as a written agreement, or via direct delivery by Erik Nørskov as part of a paid engagement (in which case the license is granted for that specific project scope only).

Each commercial license covers only the version of the software actually delivered into the licensee's project by the licensor. Later versions become covered only when likewise delivered as part of a paid engagement or written agreement, or when the licensee obtains a separate paid license for that later version. The licensee may not substitute or upgrade the software to any later version on their own initiative without such a license.

Contact: https://pxq.dk
