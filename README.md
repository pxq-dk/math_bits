# math_bits

Header-only C++20 library for multiplying integers by a constant factor — a floating-point value or an exact ratio `P/Q` — using integer bit-shifting: no FPU, no runtime division, compile-time unit tests.

---

## Features

- **No floating-point at runtime** — all FPU operations happen at compile time. The generated code is pure integer arithmetic.
- **Compile-time parameter generation** — multiplier, bit-shift count, and integer scale factor are all derived at compile time from the floating-point input.
- **Overflow safe** — the maximum multiplication factor is computed at compile time to guarantee no overflow for the given input range.
- **Configurable accuracy** — `max_error` (in the options traits class) sets the allowed deviation from the floating-point expression the scaler replaces. Defaults to ±1 LSB.
- **Exact ratios** — `mult_bitshift_ratio<P, Q, …>` scales by the exact fraction `P/Q` and is checked against exact integer math `⌊x·P/Q⌋`; division-like ratios (`1/48000`, `3300/4095`, `1/3`) are typically *proven* exact for the whole input range.
- **Runtime gains** — `mult_bitshift_ratio_dyn` takes the ratio `num/den` at runtime (calibration), with a compile-time contract for `num` and `den`; shift, overflow safety and the error bound are proven at compile time for every gain the contract allows.
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

`multvalue` must be a floating-point constant (`0.75`, `1.0 / 1000`); it is only used at compile time, and the generated code is pure integer arithmetic. For integer gains, write `x * k` directly — GCC already produces optimal code. This library is for division and fractional factors, where it replaces a slow software division (on Cortex-M0/M0+) with an inline multiply and shift.

**What `mult()` guarantees:** the same result as the floating-point expression it replaces — `(out_type)((T)x * multvalue)`, or `(out_type)((T)x * multvalue + 0.5)` with `trade_speed_for_precision` — evaluated in IEEE arithmetic of `multvalue`'s type `T` (as soft-float `double` on a Cortex-M0+ does), within `max_error` (default 1 LSB). Note that this is the *floating-point* result, quirks included: `0.7` is stored as `0.69999999999999995559…`, so in `double` code `10 * 0.7` gives `7` but `90 * 0.7` gives `62`. No fixed multiplier reproduces such a pattern exactly, so `max_error = 0` is not achievable for every constant; the compile-time check tells you.

### Exact ratios (`mult_bitshift_ratio`)

When the factor is really a fraction — downsampling by 3, ticks at 48 kHz to milliseconds, ADC counts to millivolts — give it as an exact ratio `P/Q` instead of a double:

```cpp
struct exact : mult_bitshift_options { static constexpr uint64_t max_error = 0; };

// 48 kHz ticks -> ms over the full 32-bit range: exactly ticks * 1000 / 48000, proven for every input
using ticks_to_ms = mult_bitshift_ratio<1000, 48000, 0xFFFFFFFFu, uint32_t, uint64_t, exact>;

// 12-bit ADC -> mV: exactly x * 3300 / 4095
using adc_to_mv   = mult_bitshift_ratio<3300, 4095, 4095u, uint16_t, uint32_t, exact>;

// same as mult_bitshift<math_bits_ratio{7, 10}, ...>: 90 -> 63 (the double 0.7 gives 62 in double code)
using times_0_7   = mult_bitshift_ratio<7, 10, 1000000u, uint32_t, uint64_t, exact>;
```

`mult_bitshift_ratio<P, Q, max_input_value, io_type, calc_type, Options>` is an alias for `mult_bitshift<math_bits_ratio{P, Q}, …>`; everything else (options, `mult()`, generated code) is the same as for a floating-point `multvalue`. Differences:

- **Reference:** `mult()` is checked against the exact integer result `⌊x·P/Q⌋` (with `trade_speed_for_precision`: `⌊x·P/Q + ½⌋`) — no floating-point expression and none of its quirks.
- **Exactness proof:** `x·P/Q` only has fractional parts in steps of `1/Q`, so a multiplier that runs high by less than one step can never reach the next integer: if `0 ≤ max_input_value · e < 1/Q` (`1/(2Q)` when rounding), `mult()` equals the reference for **every** input, proven without testing (the classic division-by-a-constant argument). Where the nearest multiplier would run low, the multiplier selection picks the rounded-up one. `max_error = 0` is then free.
- **Exact check where the proof does not reach:** the error at `x` depends only on the residue `x·P mod Q` and grows with `x`, so per residue the worst input is the largest one with that residue, and only residues close enough to the next integer can fail at all. The self-test checks exactly those inputs (found with the inverse of `P` mod `Q`), which verifies every input up to `max_input_value` without sampling. It is used when at most 65536 residues qualify — always the case for denominators up to about 10⁵ such as 48000, 4095 or 1000; beyond that (e.g. `Q` around 10⁹ with a large error) the sampled check of a floating-point `multvalue` is used, with its sampling limit for `max_error = 0`.
- `P/Q` is reduced (`1000/48000` → `1/48`); it must not reduce to an integer (integer multiplication is not supported — the compiler already does it optimally), and `P`, `Q` must not be 0.
- `mult_factor` is only an informational `long double` approximation of `P/Q`; `is_ratio` is `true`.

Exactness still needs enough multiplier bits: the multiplier must resolve `1/Q` steps over the whole input range, so huge input ranges need a 64-bit `calc_type` (see *Precision limit for large input ranges*) — e.g. `1/1000` over 32-bit inputs is proven exact with `calc_type = uint64_t`, but not over 40-bit inputs (only 33 multiplier bits are left; the self-test then reports the real error).

### Runtime gain (`mult_bitshift_ratio_dyn`)

When the gain is only known at runtime — e.g. an ADC gain from a calibration, `ref_mV / measured_counts` — use `mult_bitshift_ratio_dyn`. The gain is a ratio `num/den` set at runtime; what is fixed at compile time is the **contract**: the ranges of `num` and `den`.

```cpp
struct adc_cfg : mult_bitshift_ratio_dyn_options {
    static constexpr uint64_t num_min = 3000, num_max = 3600;   // contract: outside -> rejected
    static constexpr uint64_t den_min = 4000, den_max = 4095;
    static constexpr uint64_t max_error = 0;                    // proven for every allowed gain, or a build error
    using topology = math_bits_topology::SPSC<>;                // gain changed in main, mult() in an ISR (or vice versa)
};
using adc_scale = mult_bitshift_ratio_dyn<4095u, uint16_t, uint64_t, adc_cfg>;

constinit adc_scale adc{ 3300, 4095 };     // initial gain; constinit: no startup code, invalid gain = compile error

uint16_t mv = adc.mult(raw);               // as fast as the compile-time version (see Performance)

if (adc.set(cal_num, cal_den) == adc_scale::status::rejected) { /* old gain still active */ }
// or two-phase: adc.prepare(n, d) computes and checks (old gain stays active), adc.apply_new() switches
```

`mult_bitshift_ratio_dyn<max_input_value, io_type, calc_type, Options>`; the options derive from `mult_bitshift_ratio_dyn_options`, which has everything from `mult_bitshift_options` (`max_error`, `trade_speed_for_precision`, `clamp_input`, `out_type`, …) plus:

| Member | Default | Description |
|---|---|---|
| `num_min`, `num_max`, `den_min`, `den_max` | — (must be set) | The contract: `set()` / `prepare()` return `rejected` for `num` or `den` outside these ranges, and the old gain stays active. Need `den_min ≥ 1`, `num_max ≥ 1`. |
| `gain_min`, `gain_max` | no restriction | Optional gain range (`math_bits_ratio`), tighter than the contract allows: a gain outside it is clamped to the limit (`clamp_gain`, status `clamped`) or rejected. |
| `clamp_gain` | `false` | See `gain_min`, `gain_max`. |
| `topology` | `math_bits_topology::None<>` | Concurrency, with the names and members of RingBuffer_Cortex's `Topology` (whose types can be passed directly): producer = the context changing the gain, consumer = the context calling `mult()`. `None`, `SPSC`, `MPSC`, `SPMC`, `MPMC`, each with a lock type (default: PRIMASK, by inline assembly — no CMSIS needed). |

**What is proven at compile time** (for every gain the contract and the gain range allow): `max_input_value × largest gain` fits `calc_type` and `out_type`, and every result is within `max_error` of the exact `⌊x·num/den⌋` (`⌊x·num/den + ½⌋` when rounding). The multiplier is rounded up, `M = ⌈num·2^s/den⌉`, so the error is never negative, and with the fractional parts of `x·num/den` in steps of `1/den` the guarantee follows from one inequality per denominator, `max_input · (den − 1) < (max_error · den + 1) · 2^s` (`2·max_input·(den−1) < (2·max_error·den + 1)·2^s` when rounding) — linear in `den`, so checking `den_min`, `den_max` (and clamping limits) covers all. Nothing is checked per gain at runtime except the contract. `max_error = 0` needs `2^s > max_input · (den_max − 1)`: e.g. a 12-bit ADC with `den_max = 4095` needs a 64-bit `calc_type`; with `uint32_t` `den_max` up to about 256 works. If it cannot be proven, the build fails and says what to change.

| Function | Description |
|---|---|
| `mult(x)`, `x * scaler` | Multiply by the active gain. |
| `set(num, den)` | Check, compute and activate a new gain; returns `status::ok`, `clamped` or `rejected`. |
| `prepare(num, den)` / `apply_new()` | Two-phase: `prepare` checks and computes into a pending slot (active gain unchanged); `apply_new` activates it (`false` if nothing is pending). |
| `evaluate(num, den)` | Static, `constexpr`: the status and multiplier a gain would get, without changing anything. |
| `active_multiplier()` | The integer multiplier in use (for inspection). |
| `bitShifts`, `mult_factor_int_max`, `gain_lo`, `gain_hi`, `m_type` | Compile-time constants: the shift, the largest multiplier, the gain range, the multiplier type. |

**Concurrency:** `mult()` never takes a lock. A multiplier of up to 32 bits is one word, read and written atomically; a 64-bit one is kept in a double buffer with a sequence counter — the writer fills the slot readers are not using and then publishes with one atomic store; the reader re-reads if the gain changed while it read. So `SPSC` is safe in both directions (ISR writing and main reading, or the reverse). The lock (`MPSC`/`MPMC`) only serializes several producers, around the store — not around the division that computes the multiplier. The shift is chosen so the multiplier fits 32 bits whenever the error guarantee still holds there. Written for single-core Cortex-M (ordering by `volatile` accesses).

**Cost** (Cortex-M0+, `-Os`): `mult()` 5 instructions / 1 `muls` (12-bit ADC, `calc_type = uint32_t`), 14 / 2 with `calc_type = uint64_t` and a 32-bit multiplier, 43 / 5 with a 64-bit multiplier (24-bit inputs, `den` up to 10⁶); `set()` about 32–44 instructions plus one `__aeabi_uldivmod` (only when the gain changes). Storage: one or two multipliers, the pending one and a flag.

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
// Within max_error (1 LSB) of the double expression ticks * (1.0 / 48000): over this wide range
// truncation can land 1 below it (one hour -> 3599, double code gives 3600). Set
// trade_speed_for_precision for round-to-nearest (-> 3600).
```

The compile-time checks verify that `multvalue * max_input_value` fits in `out_type`. Inputs above `max_input_value` are outside the contract: without `clamp_input` their result is truncated to `out_type`. If the input is not guaranteed to stay within `max_input_value` (e.g. a free-running counter), enable `clamp_input` — such inputs then return `max_output_int` instead of a wrong value.

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
| `multvalue` | — | Floating-point multiplier (`float`, `double`, or `long double`), or an exact ratio `math_bits_ratio{P, Q}` (see `mult_bitshift_ratio`) |
| `max_input_value` | — | Maximum input value the multiplier must handle without overflow |
| `io_type` | `uint32_t` | Input integer type, and the output type unless `Options::out_type` is set. Must be unsigned. |
| `calc_type` | `uint32_t` | Internal calculation type. Must be unsigned and at least as wide as `io_type` and `out_type`. |
| `Options` | `mult_bitshift_options` | Traits-class type carrying the optional flags below. Derive from `mult_bitshift_options` and override only the members you want. |

### `mult_bitshift_options` members

All members are `static constexpr`. Override only the ones you want by deriving a new struct:

| Member | Type | Default | Description |
|---|---|---|---|
| `max_error` | `uint64_t` | `1` | Maximum allowed deviation from the floating-point expression (in LSB; see *What `mult()` guarantees*). Generalized to `uint64_t` so the struct doesn't depend on the output type; the class casts back to `out_type` internally. Must fit in `out_type`. |
| `deep_test` | `bool` | `false` | Default `false` runs a quick smoke test of up to 100 samples at compile time — fast to build. Set `true` for the full sweep (up to 65536 samples) when you want maximum assurance and can absorb the compile-time cost. |
| `clamp_input` | `bool` | `false` | If `true`, clamp inputs above `max_input_value` to `max_input_value` before multiplying — guarantees output stays within the `max_input_value * mult_factor` envelope. Adds ~5 instructions on the hot path. When `false`, the clamp disappears entirely (zero cost). |
| `trade_speed_for_precision` | `bool` | `false` | Rounding mode. `false` truncates: `mult()` targets `(out_type)(x * multvalue)`. `true` rounds half up: a half-LSB bias is added before the shift, so `mult()` targets `(out_type)(x * multvalue + 0.5)` (both in floating-point, see *What `mult()` guarantees*). Cost: one extra add per call (2–3 instructions in the 64-bit split multiply) — skipped automatically when it provably changes no result, i.e. when `max_input_value × (mult_factor_int mod 2^bitShifts) < 2^(bitShifts−1)` (integer and near-integer factors); results are identical either way. Needs 1 LSB extra headroom in `out_type` (checked at compile time); needs no room in `calc_type` — when `max_input_value × mult_factor_int + 2^(bitShifts−1)` would not fit, the same rounding is done as `((x·M >> (bitShifts−1)) + 1) >> 1` instead (identical results, full shift kept); on Cortex-M0/M0+/M23 that form is also used when it is shorter (see *Rounding without headroom*). **Changes the result, not just its accuracy:** keep `false` where floor semantics matter, e.g. elapsed whole seconds (47999 ticks at 48 kHz must be 0 s, not 1 s) or bucket/index calculations. Not available in the legacy positional form. |
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
| `mult_factor` | The original floating-point multiplier (for a ratio an informational `long double` approximation) |
| `is_ratio` | `true` when `multvalue` is an exact ratio (`math_bits_ratio`) |
| `max_input_int` | The configured maximum input value |
| `bitShifts` | Number of bits shifted in the integer multiplication |
| `mult_factor_int` | The integer multiplier `mult()` uses (see *Multiplier selection*) |
| `mult_factor_int_nearest` | The default multiplier, `mult_factor · 2^bitShifts` rounded to nearest — differs from `mult_factor_int` when the selection chose the other rounding direction |
| `max_output_int` | Precomputed `mult(max_input_int)` — the largest value `mult()` will ever return |
| `max_error` | The configured `max_error` from `Options`, cast to `out_type` |
| `max_deviation` | Same as `max_error` — kept for backwards compatibility |
| `deep_test` | The configured `deep_test` flag from `Options` |
| `clamp_input` | The configured `clamp_input` flag from `Options` |
| `trade_speed_for_precision` | The configured rounding mode from `Options` |
| `min_output_range` | The configured `min_output_range` from `Options` |
| `out_type` | The type `mult()` returns: `Options::out_type`, or `io_type` when that is `void` |
| `options` | The `Options` traits-class type itself, exposed for inspection |

---

## Design Notes

**Why bit-shifting instead of floating-point?**
On Cortex-M0/M0+ there is no FPU. A floating-point multiply compiles to a software library call — slow, non-deterministic, and unsuitable for ISRs. By computing the scale factor at compile time and using a single integer multiply + shift at runtime, the hot path becomes 2–3 instructions with deterministic latency.

**Why compile-time unit tests?**
The test suite verifies that every value in a representative sample of the input range produces a result within `max_error` of the floating-point expression (see *What `mult()` guarantees*). If the chosen `max_error` is too tight for the given multiplier and types, the build fails with a clear message — no separate test binary required. By default (`deep_test=false`) the sweep runs up to 100 samples — fast to compile and adequate for catching gross errors. Set `deep_test=true` for the full sweep (up to 65536 samples) when you want maximum assurance. The sample count is capped by the input range: when `max_input_value + 1` is at or below the cap, every input from 0 to `max_input_value` is tested exactly once (exhaustive). `max_input_value` itself is always tested.

**Error analysis and targeted gray-zone check**
Sampling alone can miss inputs: in large ranges only a fraction of inputs reach the worst-case error. So before the sweep, the error is analysed. `mult(x)` and the reference differ internally by `x·e − ρ(x)`, where `e = mult_factor_int / 2^bitShifts − multvalue` is the quantization error of the integer factor and `ρ(x)` is the IEEE rounding inside the floating-point reference (zero when every product is exactly representable, otherwise below a small power of two). With `Δ = max_input_value·|e| + ρ_bound`:

| Case | Condition | Result |
|---|---|---|
| Proven OK | `Δ ≤ max_error` | Passes — no input can exceed `max_error` |
| Proven FAIL | `mult(max_input_value)` is off by more than `max_error` | Fails — `max_input_value` is a counterexample |
| Gray zone | otherwise | Targeted check (below) |

In the gray zone only inputs with `x·|e| + ρ_bound > max_error` can fail. Within one output level (all inputs with the same reference result) the reference is constant and `mult()` only grows, so the worst overshoot is at the level's last input and the worst undershoot at its first. The check tests those ends (one end when the reference is exact, both otherwise) level by level, starting at `max_input_value`, where the multiplier's error is largest, and walking down. Up to 65536 candidates are tested with `deep_test=true` (1024 otherwise); when all candidates fit in that budget, the check is exhaustive and exact. Otherwise the configuration passes if the tested candidates pass — a remaining miss is bounded to `max_error + 1`. (Known limit: with `max_error = 0` and a very large output range — hundreds of millions of levels — scattered near-integer cases deep in the range can be missed.)

The broad sweep still runs afterwards as an independent cross-check of the implementation.

**Exact compile-time arithmetic**
`multvalue` is a binary floating-point number, so it is exactly `mantissa · 2^exponent`. The library decomposes it at compile time and derives `bitShifts` and `mult_factor_int`, runs the error analysis (proven OK, gray-zone threshold), and computes the self-test reference — the IEEE floating-point expression, emulated bit-exactly (round to nearest, ties to even, for the conversion of `x`, the product and the `+ 0.5`) — all in exact 128-bit integer arithmetic (a small built-in `constexpr` type — `arm-none-eabi` has no `__int128`). The emulation does not depend on the host's or the target's floating-point unit; it was verified against real `float`, `double` and x87 `long double` hardware arithmetic. This matters because GCC evaluates `constexpr` code with the *target's* types: on ARM, `long double` is a 53-bit `double`, which cannot represent `UINT64_MAX` or 64-bit inputs above 2^53 exactly. No runtime cost; deep self-tests compile somewhat slower. `multvalue` may be `float`, `double` or an 80-bit `long double`; types with more than 64 mantissa bits (128-bit `long double`) are rejected at compile time — use `double`.

**Multiplier selection**
`multvalue · 2^bitShifts` is generally not an integer, so the multiplier must be rounded; by default to nearest (`mult_factor_int_nearest`). The other direction — or, when the value is exactly an integer, one step above it — is sometimes more accurate: for division-like constants such as `1.0 / 1000`, `1.0 / 3` or ADC scaling `3300.0 / 4095` it can make every result equal to the floating-point expression where the nearest multiplier is 1 LSB off. Only the constant differs, so there is no runtime cost. The alternative is chosen only when it is verifiably better against the same reference and its proven worst case is not worse; on a tie the nearest multiplier is kept. The full self-test then runs on the chosen multiplier.

**Rounding without headroom**
With `trade_speed_for_precision`, `mult()` normally adds the half-LSB bias `2^(bitShifts−1)` before the shift. When the product already fills the top of `calc_type` (`max_input_value × mult_factor_int + 2^(bitShifts−1)` would overflow), it rounds as `((x·M >> (bitShifts−1)) + 1) >> 1` instead: with `x·M = q·2^(bitShifts−1) + r` and `r < 2^(bitShifts−1)` both forms equal `⌊(q+1)/2⌋`, and the `+1` cannot overflow. So rounding never costs a bit of shift (precision), and such configurations compile instead of failing the `calc_type` overflow check.

The shift form is also used where it gives shorter code: on cores that cannot add a large constant directly, the bias must first be built in a register. From `MATH_BITS_SHIFT_ROUND_MIN_BIAS` on — 256 on ARMv6-M (Cortex-M0/M0+: `adds` takes 0–255, larger needs `movs` + `lsls`), 65536 on ARMv8-M Baseline (Cortex-M23: `movw` loads 16-bit constants) — the shift form is one instruction shorter at `-Os` (1–3 at `-O2`, 6–9 at `-O0`; measured, never longer). In the 64-bit split multiply the constant compared is the part actually added (`2^(bitShifts−33)` to the high word, or `2^31` to the low word for `bitShifts = 32`). Other cores (e.g. Cortex-M3/M4/M7, whose `add` takes any power of two as an immediate) keep the add (threshold 0 = off). Predefine `MATH_BITS_SHIFT_ROUND_MIN_BIAS` to override the detection. Results are identical within the contract; inputs above `max_input_value` (without `clamp_input`) can differ, since the add can wrap in `calc_type` and the shift form does not.

**Exact ratios**
Internally the multiplier is the exact value `num/den · 2^exp`: a double is its exact binary value `mant/1 · 2^exp`, a ratio is `P/Q · 2^0` (reduced). Shift selection, multiplier candidates (`⌊P·2^s/Q⌋` and its neighbours, by exact 128-by-64-bit division), the exact quantization error `e = M/2^s − P/Q` and the gray-zone analysis all work on this value in exact integer arithmetic, so a ratio involves no floating point at all — the IEEE emulation is only used for the floating-point reference of a double `multvalue`. For a ratio the reference is exact (`ρ = 0`), the start of each output level is computed directly (`⌈n·Q/P⌉`), the `1/Q` exactness proof above proves `max_error = 0` without sampling, also for ranges far too large to test, and the residue check verifies the remaining configurations exactly. Verified against brute-force integer math on 400 random ratios × `max_error` 0/1/2 × both rounding modes: every exhaustively checkable verdict agrees with the truth.

**Why waste one extra type parameter for `calc_type`?**
The intermediate product `input * mult_factor_int` can overflow `io_type`. Using a wider `calc_type` (e.g. `uint32_t` when `io_type` is `uint16_t`) keeps the intermediate value safe and shifts back down to `out_type` at the end.

---

## Performance (STM32G051, Cortex-M0+, `-Os`)

Verified by inspecting `arm-none-eabi-g++` output for representative configurations:

- **`mult()` with `calc_type ≤ uint32_t`**: hot path is `muls + lsrs` — one integer multiply, one bit-shift. A 1–2 instruction `movs/lsls` constant-load preamble brings the total to ~4 instructions on Cortex-M0+ (the constant load is an M0+ immediate-encoding limitation, not a library limitation).
- **`mult()` with `calc_type = uint64_t`**: Cortex-M0/M0+ (ARMv6-M) and Cortex-M23 (ARMv8-M Baseline) have no 32×32→64 multiply, so a plain `uint64_t` multiply would call the runtime helper `__aeabi_lmul` (~50 instructions). On these cores `mult()` instead splits the multiply into 32-bit operations (inline, no call), bit-identical to the `uint64_t` result, whenever `out_type` is at most 32 bits: ~16 instructions / 3 `muls` for a 16-bit `io_type`, ~29 instructions / 5 `muls` for a 32-bit `io_type`, ~36 instructions / 6 `muls` for a 64-bit `io_type` (9–21 instructions when `max_input_value ≥ 2^32`, since the multiplier's high word is then 0), and fewer when the multiplier has zero parts (e.g. `0.75` → a single `muls`). With a 64-bit `out_type` the plain multiply (`__aeabi_lmul`) is used. Detection is automatic (`__ARM_ARCH_6M__` / `__ARM_ARCH_8M_BASE__`); predefine `MATH_BITS_SPLIT_MUL64` to `0` or `1` to override. On cores with `umull` (Cortex-M3/M4/M7) the plain `uint64_t` multiply is kept. `calc_type=uint32_t` is still the cheapest (~4 instructions) when its precision suffices.
- **Rounding (`trade_speed_for_precision`)** costs one add — or, on Cortex-M0/M0+/M23 when the bias constant is large, a shift and an add of 1, which is shorter there (see *Rounding without headroom*, `MATH_BITS_SHIFT_ROUND_MIN_BIAS`).
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
