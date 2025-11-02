/*
 * math_bitshifting.h
 *
 *  Created on: Oct 23, 2025
 *      Author: pxq-dk ( PxQ Technologies, https://pxq.dk )
 */
#include <limits>
#include <cmath>
#include <type_traits>
#include <cstdint>
#include <bit>

#pragma once

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

// Template class for performing multiplication by a floating-point value
// using integer bit-shifting to approximate the result efficiently.
template<auto multvalue, auto max_input_value, typename io_type=uint32_t, typename calc_type=uint32_t, bool force_inlining=false>
class mult_bitshift
{
private:
    // Define the type of the multiplier (float, double, or long double)
    using float_type = decltype(multvalue);

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
        // Compute maximum value that can fit in calc_type after shifting
        calc_type maxVal = std::numeric_limits<calc_type>::max();
        maxVal = maxVal >> (std::numeric_limits<calc_type>::digits - bitShifts);

        // Multiply floating-point factor by scaled max value
        long double mult_val_tmp = static_cast<long double>(mult_factor) * static_cast<long double>(maxVal);

        // Round to nearest integer without using std::round() (for constexpr)
        calc_type mult_val = static_cast<calc_type>(mult_val_tmp + 0.5);
        return mult_val;
    }

public:
    static constexpr bool inlined = force_inlining;
    using mult_bitshift_type = mult_bitshift<multvalue, max_input_value, io_type, calc_type, inlined>;
    // Template constants for internal calculations
    static constexpr float_type mult_factor{multvalue};        // Floating-point multiplier
    static constexpr io_type max_input_int{max_input_value};   // Maximum allowed input
    static constexpr calc_type max_mult_fact{calc_max_mult()}; // Maximum multiplication factor
    static constexpr uint8_t bitShifts{calc_bitshifts()};     // Number of bits to shift
    static constexpr calc_type mult_factor_int{calc_mult_fact_int()}; // Integer multiplier

    // Multiply an input value by the multiplier using integer arithmetic and bit-shifting
    OPT_MATH_SHIFT_INLINE static constexpr inline io_type mult_inlined(io_type input_val)
    {
        // Scale the input using integer multiplier
        calc_type output_val = static_cast<calc_type>(input_val) * mult_factor_int;
        output_val = output_val >> bitShifts; // Divide by 2^bitShifts
        return static_cast<io_type>(output_val); // Cast back to original type
    }

    // Multiply an input value by the multiplier using integer arithmetic and bit-shifting
    OPT_MATH_SHIFT static constexpr inline io_type mult_noninlined(io_type input_val)
    {
        // Scale the input using integer multiplier
        calc_type output_val = static_cast<calc_type>(input_val) * mult_factor_int;
        output_val = output_val >> bitShifts; // Divide by 2^bitShifts
        return static_cast<io_type>(output_val); // Cast back to original type
    }

    // Multiply an input value by the multiplier using integer arithmetic and bit-shifting
    OPT_MATH_SHIFT_INLINE static constexpr inline io_type mult(io_type input_val)
    {
    	// Selection between inline/non inline version
    	if constexpr(inlined)
		{
    		return mult_inlined(input_val);
		}
    	else
    	{
    		return mult_noninlined(input_val);
    	}
    }

    // Overload the * operator to use the optimized multiplication
    OPT_MATH_SHIFT_INLINE constexpr inline io_type operator*(io_type val) const
    {
        return mult(val);
    }

    // Overload the * operator to use the optimized multiplication
    OPT_MATH_SHIFT_INLINE friend constexpr inline io_type operator*(io_type val, mult_bitshift_type& rhs)
    {
    	return rhs.mult(val);
    }
};

#endif // __cplusplus
