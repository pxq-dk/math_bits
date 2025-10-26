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

#if defined(__GNUC__) || defined(__clang__)
	#define OPT_MATH_SHIFT [[gnu::optimize("Os")]]
#else
	#define OPT_MATH_SHIFT
#endif

template<auto multvalue, auto max_input_value, typename io_type=uint32_t, typename calc_type=uint32_t>
class mult_bitshift
{

private:
	typedef decltype(multvalue)	float_type;
	static constexpr calc_type calc_max_mult()
	{
		static_assert(std::is_floating_point_v<float_type>, "multvalue must be float, double, or long double");
		static_assert(std::is_unsigned_v<io_type>, "io_type must be an unsigned integer type");
		static_assert(std::is_unsigned_v<calc_type>, "calc_type must be an unsigned integer type");
		static_assert(std::is_unsigned_v<decltype(max_input_value)>, "max_input_value must be an unsigned integer type");

		static_assert(multvalue>0, "multvalue must not be negative or zero!");
		static_assert(max_input_value>0, "max_input_value must not be zero!");

		static_assert(std::numeric_limits<calc_type>::max()>=max_input_value, "max_input_value must be smaller than calc_type datasize can store!");
		static_assert(std::numeric_limits<io_type>::max()>=max_input_value, "max_input_value must be smaller than io_type datasize can store!");

		constexpr long double maxVal = static_cast<long double>(std::numeric_limits<calc_type>::max());
		constexpr long double res = maxVal/(static_cast<long double>(multvalue)*static_cast<long double>(max_input_value));
		static_assert(res <= static_cast<long double>(std::numeric_limits<calc_type>::max()), "Division result too big");

		return static_cast<calc_type>(res);
	}

	static constexpr uint8_t calc_bitshifts()
	{	// Instead of using log2(c++23 and forwards), we also want to support C++20.
		// Therefore integer constexpr function has been implemented without log2().
		static_assert(max_mult_fact>0, "max_mult_fact is probably zero, and this is not allowed!");
		return static_cast<uint8_t>(std::bit_width(max_mult_fact) - 1);
	}

	static constexpr calc_type calc_mult_fact_int()
	{
		calc_type maxVal = std::numeric_limits<calc_type>::max();
		maxVal = maxVal >> ( std::numeric_limits<calc_type>::digits-bitShifts);


		long double mult_val_tmp = static_cast<long double>(mult_factor)*(static_cast<long double>(maxVal));
		// Casting and Rounding without the use of std::round(), since in some circumstances C++23 is needed for constexpr.
		calc_type mult_val = static_cast<calc_type>(mult_val_tmp + 0.5);
		return mult_val;
	}

	static constexpr float_type mult_factor{multvalue};
	static constexpr io_type max_input_int{max_input_value};
	static constexpr calc_type max_mult_fact{calc_max_mult()};
	static constexpr uint8_t bitShifts{calc_bitshifts()};
	static constexpr calc_type mult_factor_int{calc_mult_fact_int()};

public:

	OPT_MATH_SHIFT static constexpr inline io_type mult(io_type input_val)
	{
		calc_type output_val = static_cast<calc_type>(input_val)*mult_factor_int;
		output_val = output_val>>bitShifts;
		return static_cast<io_type>(output_val);
	}

	OPT_MATH_SHIFT constexpr inline io_type operator*(io_type val) const
	{
		return mult(val);
	}
};


#endif //__cplusplus
