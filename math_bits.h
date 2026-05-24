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
#include <bit>
#include <array>

// Define a compiler-specific optimization hint for functions.
// Only applies for GCC or Clang. Other compilers ignore it.
// OPT_MATH_BITS_ prefix scopes these to this library; #undef'd at end of header.
#if defined(__GNUC__) || defined(__clang__)
    #define OPT_MATH_BITS        [[gnu::optimize("Os")]] // Optimize for size
    #define OPT_MATH_BITS_INLINE [[gnu::always_inline, gnu::optimize("Os")]] // Optimize for size, always inline
#else
    #define OPT_MATH_BITS
    #define OPT_MATH_BITS_INLINE
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
// depend on io_type; the class casts it back to io_type internally.
//
// trade_speed_for_precision (default false):
//   When false, mult() truncates — matches (io_type)(input * mult_factor) with up
//   to 1 LSB quantization noise vs the float ideal.
//   When true, mult() adds a half-LSB bias before the shift so the output matches
//   (io_type)round(input * mult_factor) — exactly, when bitShifts headroom permits.
//   Cost: one extra add per mult() call (typically 1 cycle on Cortex-M0+). With
//   sufficient headroom, max_error = 0 will compile-pass; otherwise the static
//   sweep tells you to widen calc_type or reduce max_input_value.
struct mult_bitshift_options
{
    static constexpr uint64_t max_error                 = 1;
    static constexpr bool     deep_test                 = false;
    static constexpr bool     clamp_input               = false;
    static constexpr bool     trade_speed_for_precision = false;
};

// Template class with unit testing for mult_bitshift class.
template<typename MultType, bool DeepTest = true>
class unit_test_mult_bitshift
{

public:
	using mult_type = MultType::mult_type;
    using float_type = MultType::float_type;
    using io_type = MultType::io_type;
    using calc_type = MultType::calc_type;

    static constexpr bool test_in_depth{DeepTest};

    static constexpr io_type max_deviation = MultType::max_deviation;
    static constexpr uint64_t min_loop_iterations = 100;
    static constexpr uint64_t max_loop_iterations = std::numeric_limits<uint16_t>::max();
    static constexpr uint64_t calc_loop_iterations()
    {
    	constexpr uint64_t io_type_max = static_cast<uint64_t>(std::numeric_limits<io_type>::max());

    	uint64_t iterations = 0;
    	if constexpr (io_type_max >= max_loop_iterations)
    	{	iterations = max_loop_iterations;    	}
    	else
    	{	iterations = io_type_max + 1;	}

        if constexpr(!test_in_depth)
    	{
        	iterations = min_loop_iterations;
    	}

        return iterations;
    }

    static constexpr uint64_t loop_iterations = calc_loop_iterations();

    static constexpr float_type mult_factor = MultType::mult_factor;        // Floating-point multiplier

    template <calc_type N, double start, double end>
    static constexpr std::array<double, N> linspace() {
        static_assert(N >= 2, "linspace requires N >= 2 (need at least two points to define a spacing)");

        std::array<double, N> arr{};

        const double step = (end - start) / (N - 1);

        for (calc_type i = 0; i < N; ++i)
            arr[i] = start + i * step;

        return arr;
    }

    static constexpr bool number_ok(io_type input)
    {
    	bool success = false;

    	io_type res_expected;
    	if constexpr (MultType::trade_speed_for_precision)
    	{
    		// Round-to-nearest reference to match mult()'s biased-shift output.
    		res_expected = static_cast<io_type>(
    			static_cast<long double>(input) * static_cast<long double>(mult_factor) + 0.5L);
    	}
    	else
    	{
    		// Truncation reference — matches mult()'s default floor-via-shift behavior.
    		res_expected = static_cast<io_type>(
    			static_cast<long double>(input) * static_cast<long double>(mult_factor));
    	}
    	io_type res_actual = MultType::mult(input);

    	io_type res_min,res_max;

    	if(res_expected>max_deviation)
    	{	res_min = res_expected-max_deviation;	}
    	else
    	{	res_min = 0;	}

    	if(res_expected< (std::numeric_limits<io_type>::max() -max_deviation))
    	{	res_max = res_expected+max_deviation;	}
    	else
    	{	res_max = std::numeric_limits<io_type>::max();		}

    	if(res_min>res_actual)
    	{	success = false;	}
    	else if(res_max<res_actual)
    	{	success = false;	}
    	else
    	{	success = true;	}

    	return success;
    }

    static constexpr bool run_test()
    {
    	constexpr double start = 0;
    	constexpr double stop = MultType::max_input_int;
    	constexpr calc_type elementTestCount = loop_iterations;

    	constexpr std::array<double, elementTestCount> values = linspace<elementTestCount, start, stop>();

    	// The loop below casts each `value` (double) to io_type (unsigned). A negative
    	// start would make that cast UB on the early samples — and the rest of the test
    	// class (rounding bias direction, overflow comparisons, max_input_int as `stop`)
    	// assumes non-negative inputs throughout. Revisit those before relaxing this.
    	static_assert(start >= 0, "run_test() assumes start >= 0; revisit the loop body and rest of the test class before allowing negative test values.");

    	for(auto value : values)
    	{
    		io_type input =static_cast<io_type>(value);
    		if(!number_ok(input))	return false;
    	}

    	return true;
    }
};



// Template class for performing multiplication by a floating-point value
// using integer bit-shifting to approximate the result efficiently.
//
// Options is a traits-class type carrying the optional flags (max_error,
// deep_test, clamp_input). Pass mult_bitshift_options for defaults, or
// derive a struct and override only the members you want.
template<auto multvalue, auto max_input_value,
         typename IoType=uint32_t, typename CalcType=uint32_t,
         typename Options = mult_bitshift_options>
class mult_bitshift
{
public:
    using io_type = IoType;
    using calc_type = CalcType;
    // Define the type of the multiplier (float, double, or long double)
    using float_type = decltype(multvalue);
    using options = Options;
    using mult_type = mult_bitshift<multvalue, max_input_value, io_type, calc_type, Options>;

    // Surface the values from the options traits class as plain constants so
    // the rest of the class can read them with the original short names.
    static constexpr io_type max_error                  = static_cast<io_type>(Options::max_error);
    static constexpr bool    deep_test                  = Options::deep_test;
    static constexpr bool    clamp_input                = Options::clamp_input;
    static constexpr bool    trade_speed_for_precision  = Options::trade_speed_for_precision;

    // Defense-in-depth: max_error must fit in io_type (otherwise the cast above truncates silently).
    static_assert(Options::max_error <= static_cast<uint64_t>(std::numeric_limits<io_type>::max()),
                  "Options::max_error does not fit in io_type!");

    // Calculate the maximum multiplication factor that fits into calc_type
    static constexpr calc_type calc_max_mult()
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

        // Ensure the result of mult(max_input_value) fits in io_type, with headroom for max_error
        // (and an extra LSB when trade_speed_for_precision is on, since round-half-up can bump
        //  the float ideal up by half an LSB at the io_type scale before the cast).
        static_assert(static_cast<long double>(multvalue) * static_cast<long double>(max_input_value)
                      <= static_cast<long double>(std::numeric_limits<io_type>::max() - max_error)
                         - (trade_speed_for_precision ? 1.0L : 0.0L),
                      "multvalue * max_input_value would overflow io_type (no headroom for max_error or rounding bias)!");

        // Calculate the maximum multiplication factor that won't overflow calc_type
        constexpr long double maxVal = static_cast<long double>(std::numeric_limits<calc_type>::max());
        constexpr long double res = maxVal / (static_cast<long double>(multvalue) * static_cast<long double>(max_input_value));
        static_assert(res <= static_cast<long double>(std::numeric_limits<calc_type>::max()), "Division result too big");

        return static_cast<calc_type>(res);
    }

    // Compute the number of bits needed to represent max_mult_fact
    static constexpr uint8_t calc_bitshifts()
    {
        // C++20 does not have std::log2 constexpr support, so we use bit_width
        static_assert(max_mult_fact > 0, "max_mult_fact is probably zero, and this is not allowed!");
        return static_cast<uint8_t>(std::bit_width(max_mult_fact) - 1);
    }

    // Calculate the integer multiplier used in bit-shift multiplication
    static constexpr calc_type calc_mult_fact_int()
    {
        // Scale factor is 2^bitShifts (matches the runtime >> bitShifts divide)
        calc_type maxVal = static_cast<calc_type>(1) << bitShifts;

        // Multiply floating-point factor by scaled max value
        long double mult_val_tmp = static_cast<long double>(mult_factor) * static_cast<long double>(maxVal);

        // Round to nearest integer without using std::round() (for constexpr)
        calc_type mult_val = static_cast<calc_type>(mult_val_tmp + 0.5);
        return mult_val;
    }

    // Template constants for internal calculations
    static constexpr io_type max_deviation{max_error};
    static constexpr float_type mult_factor{multvalue};        // Floating-point multiplier
    static constexpr io_type max_input_int{max_input_value};   // Maximum allowed input
    static constexpr calc_type max_mult_fact{calc_max_mult()}; // Maximum multiplication factor
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
    static_assert(static_cast<long double>(max_input_int) * static_cast<long double>(mult_factor_int)
                  + (trade_speed_for_precision ? static_cast<long double>(round_bias) : 0.0L)
                  <= static_cast<long double>(std::numeric_limits<calc_type>::max()),
                  "max_input_value * mult_factor_int (+ rounding bias if trade_speed_for_precision) would overflow calc_type — choose a wider calc_type or smaller max_input_value!");

    // Precomputed maximum output: mult(max_input_int). Used by the clamp_input early-return path,
    // and exposed publicly so callers can query the maximum value mult() will ever return.
    static constexpr io_type max_output_int =
        static_cast<io_type>((static_cast<calc_type>(max_input_int) * mult_factor_int) >> bitShifts);

    // Multiply an input value by the multiplier using integer arithmetic and bit-shifting.
    // Unconditionally always_inline so the integer multiply-and-shift fuses into the caller —
    // the previous force_inlining option flag has been retired in favor of this default.
    OPT_MATH_BITS_INLINE static constexpr io_type mult(io_type input_val)
    {
        // Optional clamp — disappears entirely when clamp_input == false. Uses early return with
        // the precomputed max_output_int to avoid the redundant uxth GCC inserts after a
        // conditional value substitution.
        if constexpr (clamp_input)
        {
            if (input_val > max_input_int) return max_output_int;
        }
        // Scale the input using integer multiplier
        calc_type output_val = static_cast<calc_type>(input_val) * mult_factor_int;
        if constexpr (trade_speed_for_precision)
        {
            // Half-LSB bias so the shift below produces round-half-up output —
            // mult() then exactly matches (io_type)round(input * mult_factor) when
            // bitShifts headroom permits.
            output_val += round_bias;
        }
        output_val = output_val >> bitShifts; // Divide by 2^bitShifts
        return static_cast<io_type>(output_val); // Cast back to original type
    }

    // Overload the * operator to use the optimized multiplication
    OPT_MATH_BITS_INLINE constexpr inline io_type operator*(io_type val) const
    {
        return mult(val);
    }

    // Overload the * operator to use the optimized multiplication
    OPT_MATH_BITS_INLINE friend constexpr inline io_type operator*(io_type val, const mult_type& rhs)
    {
    	return rhs.mult(val);
    }

    static_assert(unit_test_mult_bitshift<mult_type, deep_test>::run_test(), "Static unit-testing failed! Consider increasing max_error!");
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

// Drop the helper macros so they don't leak into translation units that include this header.
#undef OPT_MATH_BITS
#undef OPT_MATH_BITS_INLINE
