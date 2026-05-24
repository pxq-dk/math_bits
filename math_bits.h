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
#include <limits>
#include <type_traits>
#include <cstdint>
#include <bit>
#include <array>

#ifdef __cplusplus

// Define a compiler-specific optimization hint for functions.
// Only applies for GCC or Clang. Other compilers ignore it.
#if defined(__GNUC__) || defined(__clang__)
    #define OPT_MATH_SHIFT [[gnu::optimize("Os")]] // Optimize for size
    #define OPT_MATH_SHIFT_INLINE [[gnu::always_inline, gnu::optimize("Os")]] // Optimize for size
#else
    #define OPT_MATH_SHIFT
	#define OPT_MATH_SHIFT_INLINE
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
struct mult_bitshift_options
{
    static constexpr uint64_t max_error      = 1;
    static constexpr bool     deep_test      = false;
    static constexpr bool     clamp_input    = false;
};

// Template class with unit testing for mult_bitshift class.
template<typename multType, bool DeepTest = true>
class unit_test_mult_bitshift
{

public:
	using mult_type = multType::mult_type;
    using float_type = multType::float_type;
    using io_type = multType::io_type_t;
    using calc_type = multType::calc_type_t;

    static constexpr bool test_in_depth{DeepTest};

    static constexpr io_type max_deviation = multType::max_deviation;
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

    static constexpr float_type mult_factor = multType::mult_factor;        // Floating-point multiplier

    template <calc_type N, double start, double end>
    static constexpr std::array<double, N> linspace() {
        static_assert(N >= 2, "linspace requires N >= 2 (need at least two points to define a spacing)");

        std::array<double, N> arr{};

        const double step = (end - start) / (N - 1);

        for (calc_type i = 0; i < N; ++i)
            arr[i] = start + i * step;

        return arr;
    }

    static constexpr bool number_ok(io_type test_value)
    {
    	bool success = false;
    	io_type input =static_cast<io_type>(test_value);

    	io_type res_expected = static_cast<io_type>(static_cast<long double>(input) * static_cast<long double>(mult_factor));
    	io_type res_actual = multType::mult(input);

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
    	constexpr double stop = multType::max_input_int;
    	constexpr calc_type elementTestCount = loop_iterations;

    	constexpr std::array<double, elementTestCount> values = linspace<elementTestCount, start, stop>();

    	bool success = true;

    	for(auto value : values)
    	{
    		if(value<0)	value = 0;
    		io_type input =static_cast<io_type>(value);
    		bool res = number_ok(input);
    		if(!res)
    		{	success = false;	}
    	}

    	return success;
    }
};



// Template class for performing multiplication by a floating-point value
// using integer bit-shifting to approximate the result efficiently.
//
// Options is a traits-class type carrying the optional flags (max_error,
// force_inlining, deep_test, clamp_input). Pass mult_bitshift_options for
// defaults, or derive a struct and override only the members you want.
template<auto multvalue, auto max_input_value,
         typename io_type=uint32_t, typename calc_type=uint32_t,
         typename Options = mult_bitshift_options>
class mult_bitshift
{
public:
	using mult_type = mult_bitshift<multvalue, max_input_value, io_type, calc_type, Options>;
    // Define the type of the multiplier (float, double, or long double)
    using float_type = decltype(multvalue);
    using io_type_t = io_type;
    using calc_type_t = calc_type;
    using options = Options;

    // Surface the values from the options traits class as plain constants so
    // the rest of the class can read them with the original short names.
    static constexpr io_type max_error     = static_cast<io_type>(Options::max_error);
    static constexpr bool    deep_test      = Options::deep_test;
    static constexpr bool    clamp_input    = Options::clamp_input;

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
                      "max_input_value must be smaller than calc_type datasize can store!");
        static_assert(std::numeric_limits<io_type>::max() >= max_input_value,
                      "max_input_value must be smaller than io_type datasize can store!");

        // Ensure the result of mult(max_input_value) fits in io_type, with headroom for max_error
        static_assert(static_cast<long double>(multvalue) * static_cast<long double>(max_input_value)
                      <= static_cast<long double>(std::numeric_limits<io_type>::max() - max_error),
                      "multvalue * max_input_value would overflow io_type (no headroom for max_error)!");

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

    // Defense-in-depth: max_input_value * mult_factor_int must not overflow calc_type at runtime
    static_assert(static_cast<long double>(max_input_int) * static_cast<long double>(mult_factor_int)
                  <= static_cast<long double>(std::numeric_limits<calc_type>::max()),
                  "max_input_value * mult_factor_int would overflow calc_type — choose a wider calc_type or smaller max_input_value!");

    // Precomputed maximum output: mult(max_input_int). Used by the clamp_input early-return path,
    // and exposed publicly so callers can query the maximum value mult() will ever return.
    static constexpr io_type max_output_int =
        static_cast<io_type>((static_cast<calc_type>(max_input_int) * mult_factor_int) >> bitShifts);

    // Multiply an input value by the multiplier using integer arithmetic and bit-shifting
    OPT_MATH_SHIFT static constexpr inline io_type mult(io_type input_val)
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
        output_val = output_val >> bitShifts; // Divide by 2^bitShifts
        return static_cast<io_type>(output_val); // Cast back to original type
    }

    // Overload the * operator to use the optimized multiplication
    OPT_MATH_SHIFT_INLINE constexpr inline io_type operator*(io_type val) const
    {
        return mult(val);
    }

    // Overload the * operator to use the optimized multiplication
    OPT_MATH_SHIFT_INLINE friend constexpr inline io_type operator*(io_type val, const mult_type& rhs)
    {
    	return rhs.mult(val);
    }

    static_assert(unit_test_mult_bitshift<mult_type, deep_test>::run_test(), "Static unit-testing failed! Consider increasing max_error!");
};


// Helper struct for the mult_bitshift_legacy alias below.
// Bridges the old positional template arguments into the new traits-class
// shape expected by mult_bitshift's Options parameter. Not intended for
// direct use — define your own struct deriving from mult_bitshift_options instead.
template<uint64_t MaxError, bool ForceInlining, bool DeepTest, bool ClampInput>
struct mult_bitshift_legacy_options
{
    static constexpr uint64_t max_error      = MaxError;
    static constexpr bool     force_inlining = ForceInlining;
    static constexpr bool     deep_test      = DeepTest;
    static constexpr bool     clamp_input    = ClampInput;
};

// Backwards-compatibility alias preserving the old positional template signature.
// Existing code that uses mult_bitshift<..., max_error, force_inlining, deep_test, clamp_input>
// can be migrated by simply renaming to mult_bitshift_legacy<...>. New code should
// prefer the traits-class form: mult_bitshift<..., MyOpts> with MyOpts deriving from
// mult_bitshift_options.
template<auto multvalue, auto max_input_value,
         typename io_type=uint32_t, typename calc_type=uint32_t,
         io_type max_error=1, bool force_inlining=false,
         bool deep_test=true, bool clamp_input=false>
using mult_bitshift_legacy = mult_bitshift<
    multvalue, max_input_value, io_type, calc_type,
    mult_bitshift_legacy_options<static_cast<uint64_t>(max_error), force_inlining, deep_test, clamp_input>
>;

#endif // __cplusplus
