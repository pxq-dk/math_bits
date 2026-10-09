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
- **Always-inlined hot path** — `mult()` is unconditionally `[[gnu::always_inline]]`, so the integer multiply-and-shift fuses into the caller with no extra flag.

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

The optional settings (`max_error`, `deep_test`, `clamp_input`, `out_type`, …) live in a traits-class struct. Derive from `mult_bitshift_options` and override only what you want — everything else stays at its default.

```cpp
struct fast_safe : mult_bitshift_options {
    static constexpr bool deep_test   = false;   // skip deep compile-time sweep
    static constexpr bool clamp_input = true;    // clamp inputs > max_input_value
};
using scale75_safe = mult_bitshift<0.75, (uint16_t)1000, uint16_t, uint32_t, fast_safe>;

uint16_t result = scale75_safe::mult(2000); // returns mult(1000), not garbage
```

Option structs compose — derive from another option struct to extend it:

```cpp
struct fast : mult_bitshift_options {
    static constexpr bool deep_test = false;
};
struct fast_with_clamp : fast {
    static constexpr bool clamp_input = true;
};
```

### Different input and output types

By default `mult()` returns `io_type`. Set `out_type` in the options when the result needs a different width — e.g. a 64-bit tick counter scaled to a 32-bit value in seconds:

```cpp
struct u32_out : mult_bitshift_options {
    using out_type = uint32_t;                   // mult() returns uint32_t
};
// 48 kHz ticks -> seconds, ticks up to 2^40 (~265 days)
using ticks_to_s = mult_bitshift<1.0 / 48000, 1099511627775ull, uint64_t, uint64_t, u32_out>;

uint32_t seconds = ticks_to_s::mult(ticks);      // no cast; range checked at compile time
// Within max_error (1 LSB) of ticks / 48000 — truncation can land 1 below an exact value
// (one hour -> 3599). Set trade_speed_for_precision for round-to-nearest (-> 3600).
```

The compile-time checks verify that `multvalue * max_input_value` fits in `out_type`. Inputs above `max_input_value` are outside the contract: without `clamp_input` their result is truncated to `out_type`.

**Precision limit for large input ranges:** the product `max_input_value × multiplier` must fit in `calc_type`, so a very large input range leaves few bits for the multiplier. The multiplier needs roughly as many bits as the output, so `max_error ≤ 1` needs approximately `max_output × max_input_value < 2^(bits(calc_type)+1)`. In the example above: up to 2^40 ticks the worst-case error is 0.25 LSB, at 2^42 ticks it is ~15 LSB (rejected at compile time with the default `max_error`).

### Backwards-compatible legacy form

The previous positional-argument signature is preserved as `mult_bitshift_legacy`. Existing call sites can keep working by renaming `mult_bitshift` → `mult_bitshift_legacy`:

```cpp
// Same configuration as scale75_safe above, in the old positional form.
// The force_inlining parameter (position 6) is accepted for source
// compatibility but has no effect — mult() is now unconditionally
// always_inline.
using scale75_safe_legacy =
    mult_bitshift_legacy<0.75, (uint16_t)1000, uint16_t, uint32_t,
                         /*max_error*/1, /*force_inlining (unused)*/false,
                         /*deep_test*/false, /*clamp_input*/true>;
```

New code should prefer the traits-class form — the legacy form is kept only to avoid breaking existing instantiations (it always returns `io_type`; `out_type` needs the traits-class form). Note that the legacy form preserves the pre-refactor default `deep_test=true`, while the traits-class form's default is `false`; set it explicitly if the difference matters.

---

## Template Parameters

`mult_bitshift` takes five template parameters: two required values, two type parameters with defaults, and a traits-class type carrying the optional flags.

| Parameter | Default | Description |
|---|---|---|
| `multvalue` | — | Floating-point multiplier (`float`, `double`, or `long double`) |
| `max_input_value` | — | Maximum input value the multiplier must handle without overflow |
| `io_type` | `uint32_t` | Input integer type, and the output type unless `Options::out_type` is set. Must be unsigned. |
| `calc_type` | `uint32_t` | Internal calculation type. Must be unsigned and at least as wide as `io_type` and `out_type`. |
| `Options` | `mult_bitshift_options` | Traits-class type carrying the optional flags below. Derive from `mult_bitshift_options` and override only the members you want. |

### `mult_bitshift_options` members

All members are `static constexpr`. Override only the ones you want by deriving a new struct:

| Member | Type | Default | Description |
|---|---|---|---|
| `max_error` | `uint64_t` | `1` | Maximum allowed deviation from the true floating-point result (in LSB). Generalized to `uint64_t` so the struct doesn't depend on the output type; the class casts back to `out_type` internally. Must fit in `out_type`. |
| `deep_test` | `bool` | `false` | Default `false` runs a quick smoke test of up to 100 samples at compile time — fast to build. Set `true` for the full sweep (up to 65536 samples) when you want maximum assurance and can absorb the compile-time cost. |
| `clamp_input` | `bool` | `false` | If `true`, clamp inputs above `max_input_value` to `max_input_value` before multiplying — guarantees output stays within the `max_input_value * mult_factor` envelope. Adds ~5 instructions on the hot path. When `false`, the clamp disappears entirely (zero cost). |
| `min_output_range` | `uint64_t` | `1` | Minimum required output range: `multvalue * max_input_value` must be at least this many LSBs, otherwise the build fails. Output resolution is 1 LSB of `out_type`, so relative full-scale resolution is ~`1/(multvalue * max_input_value)` — e.g. set `100` to require ≥ 1%. The default `1` only rejects scalers whose every output would be below 1 LSB (which cannot compile anyway). Compile-time only, zero runtime cost. |
| `out_type` | type | `void` | Return type of `mult()`; `void` means "same as `io_type`". Set it when input and output need different widths (`using out_type = uint32_t;`) — see [Different input and output types](#different-input-and-output-types). Must be an unsigned integer type, and `multvalue * max_input_value` must fit in it (checked at compile time). |

### Legacy positional form: `mult_bitshift_legacy`

For backwards compatibility, the previous positional signature is preserved as a separate alias. The legacy form's defaults match the pre-refactor `mult_bitshift` defaults — notably `deep_test=true` (full sweep of up to 65536 samples). The new traits-class form's `deep_test` default was deliberately changed to `false` for faster compiles; if you want the deep sweep, either use the legacy form or override `deep_test=true` in your options struct.

| Position | Parameter | Default |
|---|---|---|
| 1 | `multvalue` | — |
| 2 | `max_input_value` | — |
| 3 | `io_type` | `uint32_t` |
| 4 | `calc_type` | `uint32_t` |
| 5 | `max_error` | `1` |
| 6 | `force_inlining` (no effect — kept for source compatibility) | `false` |
| 7 | `deep_test` | `true` |
| 8 | `clamp_input` | `false` |

---

## API Reference

| Function | Description |
|---|---|
| `mult(input)` | Multiply `input` (`io_type`) by the configured factor; returns `out_type`. Static — no instance needed. |
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
| `max_error` | The configured `max_error` from `Options`, cast to `out_type` |
| `max_deviation` | Same as `max_error` — kept for backwards compatibility |
| `deep_test` | The configured `deep_test` flag from `Options` |
| `clamp_input` | The configured `clamp_input` flag from `Options` |
| `min_output_range` | The configured `min_output_range` from `Options` |
| `out_type` | The type `mult()` returns: `Options::out_type`, or `io_type` when that is `void` |
| `options` | The `Options` traits-class type itself, exposed for inspection |

---

## Design Notes

**Why bit-shifting instead of floating-point?**
On Cortex-M0/M0+ there is no FPU. A floating-point multiply compiles to a software library call — slow, non-deterministic, and unsuitable for ISRs. By computing the scale factor at compile time and using a single integer multiply + shift at runtime, the hot path becomes 2–3 instructions with deterministic latency.

**Why compile-time unit tests?**
The test suite verifies that every value in a representative sample of the input range produces a result within `max_error` of the true floating-point result. If the chosen `max_error` is too tight for the given multiplier and types, the build fails with a clear message — no separate test binary required. By default (`deep_test=false`) the sweep runs up to 100 samples — fast to compile and adequate for catching gross errors. Set `deep_test=true` for the full sweep (up to 65536 samples) when you want maximum assurance. The sample count is capped by the input range: when `max_input_value + 1` is at or below the cap, every input from 0 to `max_input_value` is tested exactly once (exhaustive). `max_input_value` itself is always tested.

**Error analysis and targeted gray-zone check**
Sampling alone can miss inputs: in large ranges only a fraction of inputs reach the worst-case error. So before the sweep, the error is analysed. `mult(x)` and the reference differ internally by `x·e`, where `e = mult_factor_int / 2^bitShifts − multvalue` is the quantization error of the integer factor. The output error at `x` is therefore `floor(x·|e|)` or `ceil(x·|e|)`, and with `Δ = max_input_value·|e|`:

| Case | Condition | Result |
|---|---|---|
| Proven OK | `ceil(Δ) ≤ max_error` | Passes — no input can exceed `max_error` |
| Proven FAIL | `floor(Δ) > max_error` | Fails — `max_input_value` is a counterexample |
| Gray zone | otherwise | Targeted check (below) |

In the gray zone only inputs with `x·|e| > max_error` can fail, and only by exactly 1 LSB. Within one output level the input closest to the level boundary has the largest error, so one candidate per level is tested — starting at `max_input_value`, where the error is largest, and walking down. Up to 65536 candidates are tested with `deep_test=true` (1024 otherwise); when all candidates fit in that budget, the check is exhaustive and exact. Otherwise the configuration passes if the tested candidates pass — a remaining miss is bounded to `max_error + 1`.

The broad sweep still runs afterwards as an independent cross-check of the implementation.

**Why waste one extra type parameter for `calc_type`?**
The intermediate product `input * mult_factor_int` can overflow `io_type`. Using a wider `calc_type` (e.g. `uint32_t` when `io_type` is `uint16_t`) keeps the intermediate value safe and shifts back down to `out_type` at the end.

---

## Performance (STM32G051, Cortex-M0+, `-Os`)

Verified by inspecting `arm-none-eabi-g++` output for representative configurations:

- **`mult()` with `calc_type ≤ uint32_t`**: hot path is `muls + lsrs` — one integer multiply, one bit-shift. A 1–2 instruction `movs/lsls` constant-load preamble brings the total to ~4 instructions on Cortex-M0+ (the constant load is an M0+ immediate-encoding limitation, not a library limitation).
- **`mult()` with `calc_type = uint64_t`**: Cortex-M0/M0+ (ARMv6-M) and Cortex-M23 (ARMv8-M Baseline) have no 32×32→64 multiply, so a plain `uint64_t` multiply would call the runtime helper `__aeabi_lmul` (~50 instructions). On these cores `mult()` instead splits the multiply into 32-bit operations (inline, no call), bit-identical to the `uint64_t` result, whenever `out_type` is at most 32 bits: ~16 instructions / 3 `muls` for a 16-bit `io_type`, ~29 instructions / 5 `muls` for a 32-bit `io_type`, ~36 instructions / 6 `muls` for a 64-bit `io_type` (9–21 instructions when `max_input_value ≥ 2^32`, since the multiplier's high word is then 0), and fewer when the multiplier has zero parts (e.g. `0.75` → a single `muls`). With a 64-bit `out_type` the plain multiply (`__aeabi_lmul`) is used. Detection is automatic (`__ARM_ARCH_6M__` / `__ARM_ARCH_8M_BASE__`); predefine `MATH_BITS_SPLIT_MUL64` to `0` or `1` to override. On cores with `umull` (Cortex-M3/M4/M7) the plain `uint64_t` multiply is kept. `calc_type=uint32_t` is still the cheapest (~4 instructions) when its precision suffices.
- **No FPU instructions** in either case — zero soft-float library calls at runtime.
- **Compile-time overhead**: parameter generation and unit test run entirely at compile time — zero runtime cost.
- **`clamp_input=true`** is fully inlined into the caller (~9 instructions on the common path) thanks to the unconditional `[[gnu::always_inline]]` on `mult()`. The clamp path uses an early return with a precomputed `max_output_int`.
- **`[[gnu::flatten]]` on user code** is the strongest way to force transitive inlining at a specific call site — useful when calling `mult()` from a hot loop where you want every nested call (e.g. the operator overloads, or chained scalers) inlined alongside `mult()` itself.

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
