/*
 * math_bits.h
 *
 *  Created on: Oct 23, 2025
 *      Author: pxq-dk ( PxQ Technologies, https://pxq.dk )
 *
 *  Copyright (c) 2026 Erik Nørskov / PxQ Technologies
 *  https://pxq.dk
 *
 *  Dual License:
 *
 *  1. GNU General Public License v3.0 (GPLv3)
 *     This file is free software: you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation, version 3 of the License.
 *
 *     This file is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *     General Public License for more details: https://www.gnu.org/licenses/
 *
 *  2. Commercial License
 *     For use in proprietary or closed-source products that cannot or
 *     do not wish to comply with the GPLv3, a separate commercial license
 *     is available from PxQ Technologies — either as a written agreement,
 *     or via direct delivery by Erik Nørskov as part of a paid engagement
 *     (in which case the license is granted for that specific project scope only).
 *
 *     Each commercial license covers only the version of the software
 *     actually delivered into the licensee's project by the licensor.
 *     Later versions become covered only when likewise delivered as
 *     part of a paid engagement or written agreement, or when the
 *     licensee obtains a separate paid license for that later version.
 *     The licensee may not substitute or upgrade the software to any
 *     later version on their own initiative without such a license.
 *
 *     Contact: https://pxq.dk
 */

#pragma once

#ifndef __cplusplus
#error "math_bits.h is C++-only; include from a C++ translation unit."
#endif

#include <limits>
#include <type_traits>
#include <cstdint>
#include <cstddef>
#include <bit>
#include <array>

// Force inlining of the hot path so the integer multiply-and-shift fuses into the caller.
// GCC/Clang only (GNU attribute); other compilers ignore it. #undef'd at end of header.
#if defined(__GNUC__) || defined(__clang__)
    #define MATH_BITS_ALWAYS_INLINE [[gnu::always_inline]]
#else
    #define MATH_BITS_ALWAYS_INLINE
#endif

// Split 32x64 multiply for a 64-bit calc_type on cores without a 32x32->64 multiply instruction
// (ARMv6-M: Cortex-M0/M0+/M1, ARMv8-M Baseline: Cortex-M23). There a plain uint64_t multiply
// becomes a call to the libgcc routine __aeabi_lmul; mult() uses 32-bit operations instead.
// Results are bit-identical either way. Predefine to 0 or 1 to override the detection
// (e.g. 1 to exercise the split path in host-side tests).
#ifndef MATH_BITS_SPLIT_MUL64
    #if defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_8M_BASE__)
        #define MATH_BITS_SPLIT_MUL64 1
    #else
        #define MATH_BITS_SPLIT_MUL64 0
    #endif
#endif

// Default options for mult_bitshift, passed as a traits-class type template parameter.
// Derive from this and override only the members you want to change:
//
//     struct fast_safe : mult_bitshift_options {
//         static constexpr bool deep_test   = false;
//         static constexpr bool clamp_input = true;
//     };
//     using my_scaler = mult_bitshift<0.75, 1000u, uint32_t, uint32_t, fast_safe>;
//
// max_error is generalized to uint64_t here so the struct definition does not
// depend on the output type; the class casts it back to out_type internally.
//
// out_type (default void = same as io_type):
//   Type returned by mult(). Set it when input and output need different widths, e.g. a
//   uint64_t tick counter scaled to a uint32_t result:
//       struct ms_opts : mult_bitshift_options { using out_type = uint32_t; };
//   Must be unsigned, and multvalue * max_input_value must fit in it (compile-time checked).
//
// trade_speed_for_precision (default false):
//   When false, mult() truncates — matches (out_type)(input * mult_factor) with up
//   to 1 LSB quantization noise vs the float ideal.
//   When true, mult() adds a half-LSB bias before the shift so the output matches
//   (out_type)round(input * mult_factor) — exactly, when bitShifts headroom permits.
//   Cost: one extra add per mult() call (typically 1 cycle on Cortex-M0+). With
//   sufficient headroom, max_error = 0 will compile-pass; otherwise the static
//   sweep tells you to widen calc_type or reduce max_input_value.
//
// min_output_range (default 1):
//   Minimum required output range, i.e. multvalue * max_input_value must be at least
//   this many LSBs. Output resolution is 1 LSB of out_type, so the relative resolution at
//   full scale is ~1/(multvalue * max_input_value). Raise it to get a compile error when
//   the output would be too coarse, e.g. 100 for >= 1% full-scale resolution.
//   The default 1 only rejects scalers whose every output would be below 1 LSB.
struct mult_bitshift_options
{
    static constexpr uint64_t max_error                 = 1;
    static constexpr bool     deep_test                 = false;
    static constexpr bool     clamp_input               = false;
    static constexpr bool     trade_speed_for_precision = false;
    static constexpr uint64_t min_output_range          = 1;
    using out_type = void;
};

namespace math_bits_detail
{
    // Minimal unsigned 128-bit integer for exact compile-time arithmetic (parameter derivation and
    // the self-test reference). Used only in constant evaluation: arm-none-eabi has no __int128,
    // and its long double is a 53-bit double, which cannot even represent UINT64_MAX.
    struct u128
    {
        uint64_t hi = 0;
        uint64_t lo = 0;

        constexpr u128() = default;
        constexpr u128(uint64_t v) : hi(0), lo(v) {}
        constexpr u128(uint64_t h, uint64_t l) : hi(h), lo(l) {}

        constexpr int bit_width() const
        {
            return hi ? 64 + static_cast<int>(std::bit_width(hi)) : static_cast<int>(std::bit_width(lo));
        }

        friend constexpr bool operator==(const u128& a, const u128& b) { return a.hi == b.hi && a.lo == b.lo; }
        friend constexpr bool operator<(const u128& a, const u128& b) { return a.hi != b.hi ? a.hi < b.hi : a.lo < b.lo; }
        friend constexpr bool operator<=(const u128& a, const u128& b) { return !(b < a); }

        friend constexpr u128 operator+(const u128& a, const u128& b)
        {
            const uint64_t lo = a.lo + b.lo;
            return { a.hi + b.hi + (lo < a.lo ? 1u : 0u), lo };
        }
        friend constexpr u128 operator-(const u128& a, const u128& b)
        {
            return { a.hi - b.hi - (a.lo < b.lo ? 1u : 0u), a.lo - b.lo };
        }
        // Shifts by >= 128 give 0 (no UB).
        friend constexpr u128 operator<<(const u128& a, int n)
        {
            if (n <= 0)   return a;
            if (n >= 128) return {};
            if (n >= 64)  return { a.lo << (n - 64), 0 };
            return { (a.hi << n) | (a.lo >> (64 - n)), a.lo << n };
        }
        friend constexpr u128 operator>>(const u128& a, int n)
        {
            if (n <= 0)   return a;
            if (n >= 128) return {};
            if (n >= 64)  return { 0, a.hi >> (n - 64) };
            return { a.hi >> n, (a.lo >> n) | (a.hi << (64 - n)) };
        }
    };

    // Full 64x64 -> 128-bit product from 32-bit halves.
    constexpr u128 mul64(uint64_t a, uint64_t b)
    {
        const uint64_t a_lo = a & 0xFFFFFFFFu, a_hi = a >> 32;
        const uint64_t b_lo = b & 0xFFFFFFFFu, b_hi = b >> 32;
        const uint64_t ll = a_lo * b_lo, lh = a_lo * b_hi, hl = a_hi * b_lo, hh = a_hi * b_hi;
        const uint64_t mid = (ll >> 32) + (lh & 0xFFFFFFFFu) + (hl & 0xFFFFFFFFu); // <= 3*(2^32-1)
        return { hh + (lh >> 32) + (hl >> 32) + (mid >> 32), (mid << 32) | (ll & 0xFFFFFFFFu) };
    }

    // Exact binary decomposition of a positive, finite floating-point value: v = mant * 2^exp,
    // with mant < 2^digits(T). Multiplying or dividing by 2 is exact in binary floating point.
    struct dyadic
    {
        uint64_t mant;
        int      exp;
    };

    template<typename T>
    constexpr dyadic decompose(T v)
    {
        constexpr int D = std::numeric_limits<T>::digits; // 24 (float), 53 (double), 64 (x86 long double)
        static_assert(D <= 64, "multvalue's floating-point type has more than 64 mantissa bits (e.g. 128-bit long double); use double");
        T top = 1;
        for (int i = 0; i < D; ++i) top *= 2;              // 2^D, exact
        int e = 0;
        while (v >= top)    { v /= 2; ++e; }
        while (v < top / 2) { v *= 2; --e; }
        return { static_cast<uint64_t>(v), e };
    }

    // Sign of (f * k * 2^sh) - n, evaluated exactly: compares multvalue * k * 2^sh with integer n.
    constexpr int cmp_scaled(const dyadic& f, uint64_t k, int sh, const u128& n)
    {
        const u128 p = mul64(f.mant, k);
        const int t = f.exp + sh;
        if (t >= 0)
        {
            if (p.bit_width() + t > 128) return 1;           // p * 2^t >= 2^128 > n
            const u128 l = p << t;
            return n < l ? 1 : (l == n ? 0 : -1);
        }
        const int s = -t;                                     // compare p with n * 2^s
        if (n.bit_width() == 0) return p.bit_width() ? 1 : 0;
        if (n.bit_width() + s > 128) return -1;               // n * 2^s >= 2^128 > p
        const u128 r = n << s;
        return r < p ? 1 : (p == r ? 0 : -1);
    }
}

// Template class with unit testing for mult_bitshift. DeepTest=true is the
// backwards-compat default for external `unit_test_mult_bitshift<some_mult>` callers.
template<typename MultType, bool DeepTest = true>
class unit_test_mult_bitshift
{

public:
	using mult_type = MultType::mult_type;
    using float_type = MultType::float_type;
    using io_type = MultType::io_type;
    using out_type = MultType::out_type;
    using calc_type = MultType::calc_type;

    static constexpr bool test_in_depth{DeepTest};

    static constexpr out_type max_deviation = MultType::max_deviation;
    static constexpr uint64_t min_loop_iterations = 100;
    // 65536 = every uint16_t input, so a full 16-bit range is tested exhaustively.
    static constexpr uint64_t max_loop_iterations = static_cast<uint64_t>(std::numeric_limits<uint16_t>::max()) + 1;
    // Sample count for the sweep over [0, max_input_int]. Based on the input range rather than
    // io_type's range: when max_input_int + 1 fits under the cap, every input is tested exactly
    // once (linspace step is then exactly 1); otherwise the range is sampled.
    static constexpr std::size_t calc_loop_iterations()
    {
    	constexpr uint64_t cap = test_in_depth ? max_loop_iterations : min_loop_iterations;
    	// Compare before adding 1: max_input_int + 1 would wrap to 0 for a uint64_t max input.
    	constexpr uint64_t max_in = static_cast<uint64_t>(MultType::max_input_int);

    	return static_cast<std::size_t>(max_in < cap ? max_in + 1 : cap);
    }

    static constexpr std::size_t loop_iterations = calc_loop_iterations();

    static constexpr float_type mult_factor = MultType::mult_factor;        // Floating-point multiplier

    // N is std::size_t, not calc_type: a narrow calc_type (e.g. uint8_t) would truncate the count.
    template <std::size_t N, double start, double end>
    static constexpr std::array<double, N> linspace() {
        static_assert(N >= 2, "linspace requires N >= 2 (need at least two points to define a spacing)");

        std::array<double, N> arr{};

        const double step = (end - start) / (N - 1);

        for (std::size_t i = 0; i < N; ++i)
            arr[i] = start + i * step;

        // Pin the endpoint exactly. Float (end-start)/(N-1) and i*step don't round-trip in general,
        // so arr[N-1] could land slightly below `end`. The endpoint is exactly where the
        // bitshift-multiply's input-scaled error term peaks; missing it by 1 LSB after the
        // static_cast<io_type>(value) cast would silently weaken the static_assert sweep's
        // coverage. arr[0] is already exact because `start + 0*step` is exactly `start`.
        arr[N-1] = end;

        return arr;
    }

    // Reference result for `input`: floor(input * multvalue + bias), bias = 0.5 when
    // trade_speed_for_precision (round-to-nearest), else 0 (truncation). reference_bias is only used
    // for the floating-point starting guess in level_start(); reference() itself is exact.
    static constexpr long double reference_bias = MultType::trade_speed_for_precision ? 0.5L : 0.0L;

    // Computed exactly in 128-bit integers from multvalue = mant * 2^exp (input * mant needs up to
    // 128 bits), so it stays exact for 64-bit inputs and for targets with a 53-bit long double.
    static constexpr out_type reference(io_type input)
    {
    	using math_bits_detail::u128;
    	const math_bits_detail::dyadic f = MultType::f_dyadic;
    	u128 p = math_bits_detail::mul64(static_cast<uint64_t>(input), f.mant);
    	if (f.exp >= 0)
    	{
    		p = p << f.exp;                       // integer result; the +0.5 bias cannot change floor()
    	}
    	else
    	{
    		const int k = -f.exp;                 // floor(p / 2^k + bias)
    		if constexpr (MultType::trade_speed_for_precision) p = p + (u128(1) << (k - 1));
    		p = p >> k;
    	}
    	return static_cast<out_type>(p.lo);
    }

    static constexpr bool number_ok(io_type input)
    {
    	const out_type res_expected = reference(input);
    	const out_type res_actual = MultType::mult(input);

    	const out_type res_min = (res_expected > max_deviation)
    		? static_cast<out_type>(res_expected - max_deviation)
    		: out_type{0};
    	const out_type res_max = (res_expected < std::numeric_limits<out_type>::max() - max_deviation)
    		? static_cast<out_type>(res_expected + max_deviation)
    		: std::numeric_limits<out_type>::max();

    	return (res_actual >= res_min) && (res_actual <= res_max);
    }

    static constexpr bool run_test()
    {
    	constexpr double start = 0;
    	constexpr double stop = MultType::max_input_int;
    	constexpr std::size_t elementTestCount = loop_iterations;

    	constexpr std::array<double, elementTestCount> values = linspace<elementTestCount, start, stop>();

    	// The loop below casts each `value` (double) to io_type (unsigned). A negative
    	// start would make that cast UB on the early samples — and the rest of the test
    	// class (rounding bias direction, overflow comparisons, max_input_int as `stop`)
    	// assumes non-negative inputs throughout. Revisit those before relaxing this.
    	static_assert(start >= 0, "run_test() assumes start >= 0; revisit the loop body and rest of the test class before allowing negative test values.");

    	for(auto value : values)
    	{
    		// `stop` can round up past max_input_int for a 64-bit input (double has 53 bits),
    		// and casting a double >= 2^64 to uint64_t is UB — so map the endpoint back exactly.
    		io_type input = (value >= stop) ? MultType::max_input_int : static_cast<io_type>(value);
    		if(!number_ok(input))	return false;
    	}

    	return true;
    }

    // ---- Error analysis + targeted gray-zone check ---------------------------------------
    //
    // mult(x) = floor(x*M/2^s + b) and reference(x) = floor(x*f + b) share the same bias b, so
    // they differ internally by delta(x) = x*e, with e = M/2^s - f (quantization error of the
    // integer factor). The output error at x is therefore floor(x*|e|) or ceil(x*|e|), and with
    // worst_deviation = max_input * |e|:
    //   - ceil(worst_deviation) <= max_error  -> proven OK for every input (no check needed)
    //   - floor(worst_deviation) > max_error  -> proven FAIL; max_input itself is a counterexample
    //   - otherwise (gray zone)               -> only x > max_error/|e| can exceed max_error,
    //     and only by exactly 1 LSB. Within one output level n (= reference(x)) the input closest
    //     to the level boundary dominates: the largest x of the level when e > 0 (mult runs
    //     high), the smallest x when e < 0 (mult runs low). So one candidate per level is exact.
    //
    // Not applied when bitShifts == 0 with trade_speed_for_precision: round_bias is then 0
    // while the reference still adds 0.5, so the shared-bias premise does not hold.
    static constexpr bool analysis_applies =
    	!(MultType::trade_speed_for_precision && MultType::bitShifts == 0);

    static constexpr long double quant_error =
    	static_cast<long double>(MultType::mult_factor_int)
    		/ static_cast<long double>(static_cast<uint64_t>(1) << MultType::bitShifts)
    	- static_cast<long double>(mult_factor);
    static constexpr long double abs_quant_error = quant_error < 0 ? -quant_error : quant_error;
    static constexpr long double worst_deviation =
    	static_cast<long double>(MultType::max_input_int) * abs_quant_error;

    // Small relative margin so values within float rounding of an integer go to the gray zone,
    // where they are decided by actual evaluation instead of by the analysis alone.
    static constexpr bool proven_ok = analysis_applies
    	&& worst_deviation * (1.0L + 1e-9L) <= static_cast<long double>(max_deviation);

    // Gray-zone candidate budget: up to 65536 with deep_test, else the 1024 candidates with the
    // largest expected error (closest to max_input, where the violation window is widest).
    // Separate from the 100-sample quick sweep: it only runs in the (rare) gray zone, so a
    // larger quick budget costs little compile time where it is not needed.
    static constexpr uint64_t gray_zone_quick_budget = 1024;
    static constexpr uint64_t gray_zone_budget = test_in_depth ? max_loop_iterations : gray_zone_quick_budget;

    // The endpoint has the largest |delta|; if it fails, the configuration is proven bad.
    static constexpr bool endpoint_ok() { return number_ok(MultType::max_input_int); }

    // Smallest x in [0, max_input] with reference(x) >= n. The float guess is only a starting
    // point; the loops correct it against reference(), which is monotonic in x.
    static constexpr uint64_t level_start(uint64_t n)
    {
    	constexpr uint64_t x_max = MultType::max_input_int;
    	const long double g = (static_cast<long double>(n) - reference_bias) / static_cast<long double>(mult_factor);
    	uint64_t x = 0;
    	if (g > 0)
    	{
    		const uint64_t t = (g >= static_cast<long double>(x_max)) ? x_max : static_cast<uint64_t>(g);
    		x = (static_cast<long double>(t) < g && t < x_max) ? t + 1 : t;   // ceil, clamped
    	}
    	while (x > 0 && reference(static_cast<io_type>(x - 1)) >= n) --x;
    	while (x < x_max && reference(static_cast<io_type>(x)) < n) ++x;
    	return x;
    }

    static constexpr bool gray_zone_ok()
    {
    	if constexpr (!analysis_applies || proven_ok) return true;
    	else
    	{
    		constexpr uint64_t x_max = MultType::max_input_int;
    		// Inputs with x*|e| <= max_error cannot violate; margin keeps float rounding safe.
    		constexpr long double x_crit = static_cast<long double>(max_deviation) / abs_quant_error;
    		constexpr uint64_t x_lo = (x_crit * (1.0L - 1e-9L) >= static_cast<long double>(x_max))
    			? x_max : static_cast<uint64_t>(x_crit * (1.0L - 1e-9L));

    		uint64_t checked = 0;
    		if constexpr (quant_error > 0)
    		{
    			// Largest x of each level, walking down from max_input.
    			uint64_t x = x_max;
    			while (checked < gray_zone_budget && x >= x_lo)
    			{
    				if (!number_ok(static_cast<io_type>(x))) return false;
    				++checked;
    				const uint64_t start = level_start(reference(static_cast<io_type>(x)));
    				if (start == 0 || start - 1 < x_lo) break;
    				x = start - 1;
    			}
    		}
    		else
    		{
    			// Smallest x of each level, walking down from the top level.
    			uint64_t x = level_start(reference(static_cast<io_type>(x_max)));
    			while (checked < gray_zone_budget && x >= x_lo)
    			{
    				if (!number_ok(static_cast<io_type>(x))) return false;
    				++checked;
    				if (x == 0 || x - 1 < x_lo) break;
    				x = level_start(reference(static_cast<io_type>(x - 1)));
    			}
    		}
    		return true;
    	}
    }

    static constexpr bool endpoint_passed  = endpoint_ok();
    static constexpr bool gray_zone_passed = gray_zone_ok();
};



// Template class for performing multiplication by a floating-point value
// using integer bit-shifting to approximate the result efficiently.
//
// Options is a traits-class type carrying the optional settings (see
// mult_bitshift_options). Pass mult_bitshift_options for defaults, or
// derive a struct and override only the members you want.
template<auto multvalue, auto max_input_value,
         typename IoType=uint32_t, typename CalcType=uint32_t,
         typename Options = mult_bitshift_options>
class mult_bitshift
{
public:
    using io_type = IoType;
    using calc_type = CalcType;
    // Output type: Options::out_type, or io_type when that is void (the default).
    using out_type = std::conditional_t<std::is_void_v<typename Options::out_type>,
                                        IoType, typename Options::out_type>;
    // Define the type of the multiplier (float, double, or long double)
    using float_type = decltype(multvalue);
    using options = Options;
    using mult_type = mult_bitshift<multvalue, max_input_value, io_type, calc_type, Options>;

    // Surface the values from the options traits class as plain constants so
    // the rest of the class can read them with the original short names.
    static constexpr out_type max_error                 = static_cast<out_type>(Options::max_error);
    static constexpr bool    deep_test                  = Options::deep_test;
    static constexpr bool    clamp_input                = Options::clamp_input;
    static constexpr bool    trade_speed_for_precision  = Options::trade_speed_for_precision;
    static constexpr uint64_t min_output_range          = Options::min_output_range;

    static_assert(std::is_integral_v<out_type> && std::is_unsigned_v<out_type>,
                  "Options::out_type must be void (= io_type) or an unsigned integer type");

    // Defense-in-depth: max_error must fit in out_type (otherwise the cast above truncates silently).
    static_assert(Options::max_error <= static_cast<uint64_t>(std::numeric_limits<out_type>::max()),
                  "Options::max_error does not fit in out_type!");

    // multvalue = f_dyadic.mant * 2^f_dyadic.exp, exactly. The parameter derivation below works on
    // this in exact integer arithmetic (math_bits_detail), so no long double rounding is involved.
    static constexpr math_bits_detail::dyadic f_dyadic = math_bits_detail::decompose(multvalue);

    // Sign of (multvalue * max_input_value * 2^sh) - n, evaluated exactly.
    static constexpr int cmp_product(int sh, math_bits_detail::u128 n)
    {
        return math_bits_detail::cmp_scaled(f_dyadic, static_cast<uint64_t>(max_input_value), sh, n);
    }

    // Validate the parameters, then return the shift: the largest s < digits(calc_type) with
    // multvalue * max_input_value * 2^s <= max(calc_type), so the multiplier gets every bit of
    // calc_type that the input range leaves free.
    static constexpr uint8_t calc_bitshifts()
    {
        // Validate template parameters at compile-time
        static_assert(std::is_floating_point_v<float_type>, "multvalue must be float, double, or long double");
        static_assert(std::is_unsigned_v<io_type>, "io_type must be an unsigned integer type");
        static_assert(std::is_unsigned_v<calc_type>, "calc_type must be an unsigned integer type");
        static_assert(std::is_unsigned_v<decltype(max_input_value)>, "max_input_value must be an unsigned integer type");

        static_assert(multvalue>0, "multvalue must not be negative or zero!");
        static_assert(max_input_value>0, "max_input_value must not be zero!");

        // Ensure max_input_value fits into io_type and calc_type
        static_assert(std::numeric_limits<calc_type>::max() >= max_input_value,
                      "max_input_value must fit in calc_type!");
        static_assert(std::numeric_limits<io_type>::max() >= max_input_value,
                      "max_input_value must fit in io_type!");

        // Ensure the result of mult(max_input_value) fits in out_type, with headroom for max_error
        // (and an extra LSB when trade_speed_for_precision is on, since round-half-up can bump
        //  the float ideal up by half an LSB at the out_type scale before the cast).
        static_assert(static_cast<uint64_t>(std::numeric_limits<out_type>::max()) - max_error
                          >= (trade_speed_for_precision ? 1u : 0u)
                      && cmp_product(0, static_cast<uint64_t>(std::numeric_limits<out_type>::max()) - max_error
                                         - (trade_speed_for_precision ? 1u : 0u)) <= 0,
                      "multvalue * max_input_value would overflow out_type (no headroom for max_error or rounding bias)!");

        // Reject scalers: if the product is < 1, every valid input maps to an output
        // below 1 LSB (always 0 when truncating; at most 0/1 with trade_speed_for_precision).
        static_assert(cmp_product(0, 1) >= 0,
                      "multvalue * max_input_value < 1: every ideal output would be below 1 LSB. "
                      "Scale the result up (e.g. output in milli-units) or raise max_input_value.");

        // Opt-in resolution requirement (Options::min_output_range). The hard limit above
        // stays separate so a min_output_range of 0 cannot bypass it; values <= 1 are already
        // covered by it, so they are skipped here to avoid a duplicate diagnostic.
        static_assert(min_output_range <= 1 || cmp_product(0, min_output_range) >= 0,
                      "multvalue * max_input_value < Options::min_output_range: output too coarse. "
                      "Scale the result up (e.g. output in milli-units) or lower min_output_range.");

        constexpr uint64_t calc_max = static_cast<uint64_t>(std::numeric_limits<calc_type>::max());
        static_assert(cmp_product(0, calc_max) <= 0,
                      "multvalue * max_input_value exceeds the range of calc_type — choose a wider calc_type!");

        int s = std::numeric_limits<calc_type>::digits - 1;
        while (s > 0 && cmp_product(s, calc_max) > 0) --s;
        return static_cast<uint8_t>(s);
    }

    // Integer multiplier: round-half-up(multvalue * 2^bitShifts), computed exactly. It fits
    // calc_type, since multvalue * 2^bitShifts <= max(calc_type) / max_input_value.
    static constexpr calc_type calc_mult_fact_int()
    {
        using math_bits_detail::u128;
        const int t = f_dyadic.exp + bitShifts;
        u128 m = f_dyadic.mant;
        if (t >= 0) m = m << t;
        else        m = (m + (u128(1) << (-t - 1))) >> -t;
        return static_cast<calc_type>(m.lo);
    }

    // Template constants for internal calculations
    static constexpr out_type max_deviation{max_error};
    static constexpr float_type mult_factor{multvalue};        // Floating-point multiplier
    static constexpr io_type max_input_int{max_input_value};   // Maximum allowed input
    static constexpr uint8_t bitShifts{calc_bitshifts()};     // Number of bits to shift

    // Defense-in-depth: shift count must be < calc_type width for well-defined shift behavior
    static_assert(bitShifts < std::numeric_limits<calc_type>::digits,
                  "bitShifts must be < digits(calc_type) — required for well-defined shift behavior!");

    static constexpr calc_type mult_factor_int{calc_mult_fact_int()}; // Integer multiplier

    // Half-LSB rounding bias used by mult() when trade_speed_for_precision is enabled.
    // Zero when bitShifts == 0 (no shift, no rounding needed) — guards against UB on `1 << -1`.
    // Referenced by both mult() and the calc_type overflow assert below.
    static constexpr calc_type round_bias = (bitShifts == 0)
        ? static_cast<calc_type>(0)
        : (static_cast<calc_type>(1) << (bitShifts - 1));

    // Defense-in-depth: max_input_value * mult_factor_int (+ round_bias when the rounding
    // path is active) must not overflow calc_type at runtime
    static_assert(math_bits_detail::mul64(static_cast<uint64_t>(max_input_int), static_cast<uint64_t>(mult_factor_int))
                  + math_bits_detail::u128(trade_speed_for_precision ? static_cast<uint64_t>(round_bias) : uint64_t{0})
                  <= math_bits_detail::u128(static_cast<uint64_t>(std::numeric_limits<calc_type>::max())),
                  "max_input_value * mult_factor_int (+ rounding bias if trade_speed_for_precision) would overflow calc_type — choose a wider calc_type or smaller max_input_value!");

    // Precomputed maximum output: mult(max_input_int). Used by the clamp_input early-return path,
    // and exposed publicly so callers can query the maximum value mult() will ever return.
    // Must mirror mult()'s formula exactly (including the round_bias when
    // trade_speed_for_precision is on), otherwise the clamp boundary is non-monotonic
    // (mult(max_input_int+1) would step down from mult(max_input_int)).
    static constexpr out_type max_output_int =
        static_cast<out_type>(
            ((static_cast<calc_type>(max_input_int) * mult_factor_int)
                + (trade_speed_for_precision ? round_bias : static_cast<calc_type>(0))) >> bitShifts);

    // Split multiply is used for a 64-bit calc_type with out_type <= 32 bits when
    // MATH_BITS_SPLIT_MUL64 is set (auto-detected for ARMv6-M / ARMv8-M Baseline).
    // io_type may be up to 64 bits.
    static constexpr bool use_split_mul64 = (MATH_BITS_SPLIT_MUL64 != 0)
        && std::numeric_limits<calc_type>::digits == 64
        && std::numeric_limits<out_type>::digits <= 32
        && std::numeric_limits<io_type>::digits <= 64;

    // (x * mult_factor_int [+ round_bias]) >> bitShifts for a 64-bit calc_type, using 32-bit
    // operations only. With M = M_hi*2^32 + M_lo and x = x_hi*2^32 + x_lo:
    //   x*M = x_lo*M_lo + (x_lo*M_hi + x_hi*M_lo)*2^32   (mod 2^64)
    // computed modulo 2^64 exactly like the plain uint64_t expression (so bit-identical, also
    // for inputs above max_input_value):
    //   - x_lo*M_lo needs all 64 bits -> 16x16 partial products (2 when io_type <= 16 bits, else 4)
    //   - the cross terms only contribute their low 32 bits -> one 32-bit multiply each;
    //     x_hi*M_lo exists only for a 64-bit io_type
    // mult_factor_int is a compile-time constant, so zero parts fold away (0.75 -> a single muls;
    // M_hi is always 0 when max_input_value >= 2^32).
    MATH_BITS_ALWAYS_INLINE static constexpr out_type mult_split64(io_type input_val)
    {
        constexpr uint32_t m_lo = static_cast<uint32_t>(mult_factor_int);
        constexpr uint32_t m_hi = static_cast<uint32_t>(static_cast<uint64_t>(mult_factor_int) >> 32);
        constexpr uint32_t ml = m_lo & 0xFFFFu;
        constexpr uint32_t mh = m_lo >> 16;
        const uint32_t x = static_cast<uint32_t>(input_val); // x_lo

        // x_lo * M_lo as the 64-bit pair hi:lo
        uint32_t lo, hi;
        if constexpr (std::numeric_limits<io_type>::digits <= 16)
        {
            const uint32_t ll = x * ml, lh = x * mh;
            const uint32_t mid = (ll >> 16) + (lh & 0xFFFFu);
            hi = (lh >> 16) + (mid >> 16);
            lo = (mid << 16) | (ll & 0xFFFFu);
        }
        else
        {
            const uint32_t xl = x & 0xFFFFu, xh = x >> 16;
            const uint32_t ll = xl * ml, lh = xl * mh, hl = xh * ml;
            const uint32_t mid = (ll >> 16) + (lh & 0xFFFFu) + (hl & 0xFFFFu); // <= 3*0xFFFF, no overflow
            hi = xh * mh + (lh >> 16) + (hl >> 16) + (mid >> 16);
            lo = (mid << 16) | (ll & 0xFFFFu);
        }
        hi += x * m_hi; // low 32 bits of x_lo*M_hi, shifted up by 32 (mod 2^64)
        if constexpr (std::numeric_limits<io_type>::digits > 32)
        {
            const uint32_t x_hi = static_cast<uint32_t>(static_cast<uint64_t>(input_val) >> 32);
            hi += x_hi * m_lo; // low 32 bits of x_hi*M_lo, shifted up by 32 (mod 2^64)
        }

        if constexpr (trade_speed_for_precision)
        {
            constexpr uint32_t b_lo = static_cast<uint32_t>(round_bias);
            constexpr uint32_t b_hi = static_cast<uint32_t>(static_cast<uint64_t>(round_bias) >> 32);
            lo += b_lo;
            hi += b_hi + (lo < b_lo ? 1u : 0u); // + carry out of the low word
        }

        // The output fits out_type (<= 32 bits), so multvalue * max_input_value < 2^32, and the
        // largest shift with multvalue * max_input_value * 2^bitShifts <= 2^64 - 1 is >= 32: the
        // shift always drops the whole low word, and only hi is needed.
        static_assert(bitShifts >= 32, "split multiply assumes bitShifts >= 32 (guaranteed for out_type <= 32 bits)");
        return static_cast<out_type>(hi >> (bitShifts - 32));
    }

    // Multiply an input value by the multiplier using integer arithmetic and bit-shifting.
    // Unconditionally always_inline so the integer multiply-and-shift fuses into the caller —
    // the previous force_inlining option flag has been retired in favor of this default.
    MATH_BITS_ALWAYS_INLINE static constexpr out_type mult(io_type input_val)
    {
        // Optional clamp — disappears entirely when clamp_input == false. Uses early return with
        // the precomputed max_output_int to avoid the redundant uxth GCC inserts after a
        // conditional value substitution.
        if constexpr (clamp_input)
        {
            if (input_val > max_input_int) return max_output_int;
        }
        if constexpr (use_split_mul64)
        {
            return mult_split64(input_val);
        }
        else
        {
            // Scale the input using integer multiplier
            calc_type output_val = static_cast<calc_type>(input_val) * mult_factor_int;
            if constexpr (trade_speed_for_precision)
            {
                // Half-LSB bias so the shift below produces round-half-up output —
                // mult() then exactly matches (out_type)round(input * mult_factor) when
                // bitShifts headroom permits.
                output_val += round_bias;
            }
            output_val = output_val >> bitShifts; // Divide by 2^bitShifts
            return static_cast<out_type>(output_val); // Cast to the output type
        }
    }

    // Overload the * operator to use the optimized multiplication
    MATH_BITS_ALWAYS_INLINE constexpr inline out_type operator*(io_type val) const
    {
        return mult(val);
    }

    // Overload the * operator to use the optimized multiplication
    MATH_BITS_ALWAYS_INLINE friend constexpr inline out_type operator*(io_type val, const mult_type& rhs)
    {
    	return rhs.mult(val);
    }

    // Compile-time self-test, in order: endpoint (proven-fail case), targeted gray-zone check
    // from the error analysis, then the broad sweep as an independent cross-check. Later checks
    // are skipped once an earlier one has failed, so only the most specific error is reported.
    using self_test = unit_test_mult_bitshift<mult_type, deep_test>;
    static_assert(self_test::endpoint_passed,
                  "mult(max_input_value) exceeds max_error: the error grows with the input, so the range is too large for this max_error. Increase max_error, widen calc_type or reduce max_input_value!");
    static_assert(!self_test::endpoint_passed || self_test::gray_zone_passed,
                  "Targeted error-analysis check failed: inputs near max_input_value exceed max_error by 1 LSB. Increase max_error, widen calc_type or reduce max_input_value!");
    static_assert(!self_test::endpoint_passed || !self_test::gray_zone_passed || self_test::run_test(),
                  "Static unit-testing failed! Consider increasing max_error!");
};


// Helper struct for the mult_bitshift_legacy alias below.
// Bridges the old positional template arguments into the new traits-class
// shape expected by mult_bitshift's Options parameter. Not intended for
// direct use — define your own struct deriving from mult_bitshift_options instead.
// Inherits from mult_bitshift_options so any future option (e.g. trade_speed_for_precision)
// is auto-picked-up at its default value without needing maintenance here.
template<uint64_t MaxError, bool DeepTest, bool ClampInput>
struct mult_bitshift_legacy_options : mult_bitshift_options
{
    static constexpr uint64_t max_error   = MaxError;
    static constexpr bool     deep_test   = DeepTest;
    static constexpr bool     clamp_input = ClampInput;
    // trade_speed_for_precision inherited as false from mult_bitshift_options.
};

// Backwards-compatibility alias preserving the old positional template signature.
// Existing code that uses mult_bitshift<..., max_error, force_inlining, deep_test, clamp_input>
// can be migrated by simply renaming to mult_bitshift_legacy<...>. New code should
// prefer the traits-class form: mult_bitshift<..., MyOpts> with MyOpts deriving from
// mult_bitshift_options.
//
// Note: the force_inlining parameter at position 6 is accepted for source
// compatibility but has no effect — mult() is now unconditionally always_inline.
template<auto multvalue, auto max_input_value,
         typename IoType=uint32_t, typename CalcType=uint32_t,
         IoType max_error=1, bool /*force_inlining (unused)*/ =false,
         bool deep_test=true, bool clamp_input=false>
using mult_bitshift_legacy = mult_bitshift<
    multvalue, max_input_value, IoType, CalcType,
    mult_bitshift_legacy_options<static_cast<uint64_t>(max_error), deep_test, clamp_input>
>;

// D1 regression — locks in the invariant that the clamp boundary is monotonic
// when both clamp_input and trade_speed_for_precision are on. Pre-fix, this assert
// would fire because max_output_int was computed via the truncating formula while
// mult() applied the rounding bias — the boundary stepped down by 1 LSB.
namespace math_bits_d1_regression
{
    struct opts : mult_bitshift_options
    {
        static constexpr bool clamp_input               = true;
        static constexpr bool trade_speed_for_precision = true;
    };
    using probe = mult_bitshift<0.7, 10u, uint8_t, uint16_t, opts>;
    static_assert(probe::mult(probe::max_input_int) == probe::max_output_int,
        "D1 regression: mult(max_input_int) must equal max_output_int — clamp boundary must not step down.");
}

// Drop the helper macro so it doesn't leak into translation units that include this header.
#undef MATH_BITS_ALWAYS_INLINE
