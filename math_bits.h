/*
 * math_bitshifting.h
 *
 *  Created on: Oct 23, 2025
 *      Author: pxq-dk ( PxQ Technologies, https://pxq.dk )
 */
#include <limits>
#include <cmath>
#include <type_traits>

#pragma once

#ifdef __cplusplus

#if defined(__GNUC__) || defined(__clang__)
	#define OPT_MATH_SHIFT [[gnu::optimize("Os")]]
#else
	#define OPT_MATH_SHIFT
#endif

template<auto multvalue, uint32_t max_input_value, typename io_type=uint32_t, typename calc_type=uint32_t>
class mult_bitshift
{

private:
	typedef decltype(multvalue)	float_type;
	static constexpr calc_type calc_max_mult(float_type multFact, io_type max_input_value_)
	{
		// Check whether the datatype of multvalue is allowed.
		static_assert(	(std::is_same<float,float_type>() || \
						std::is_same<double,float_type>() || \
						std::is_same<long double,float_type>()),	 \
						"multvalue datatype must be either float or double!");

		static_assert(	(std::is_same<uint8_t,io_type>() || \
						std::is_same<uint16_t,io_type>() || \
						std::is_same<uint32_t,io_type>() || \
						std::is_same<uint64_t,io_type>()),	 \
						"io_type must be either uint8_t, uint16_t, uint32_t or uint64_t!");

		calc_type maxVal = std::numeric_limits<calc_type>::max();
		float_type tmp_res = static_cast<float_type>(maxVal/(multFact*static_cast<float_type>(max_input_value_)));
		return static_cast<calc_type>(tmp_res);
	}

	static constexpr uint8_t calc_bitshifts(calc_type value)
	{	// Instead of using log2(c++23 and forwards), we also want to support C++20.
		// Therefore integer constexpr function has been implemented without log2().
		uint8_t shifts = 0;
		while (value > 1) {
			value >>= 1;
			++shifts;
		}
		return shifts;
	}

	static constexpr calc_type calc_mult_fact_int(float_type multFact, uint8_t bitshifts)
	{
		calc_type maxVal = std::numeric_limits<calc_type>::max();
		maxVal = maxVal >> ( std::numeric_limits<calc_type>::digits-bitshifts);
		calc_type mult_val = static_cast<calc_type>(std::round((multFact*(static_cast<float_type>(maxVal)))));
		return mult_val;
	}

	static constexpr float_type mult_factor{multvalue};
	static constexpr io_type max_input_int{max_input_value};
	static constexpr calc_type max_mult_fact{calc_max_mult(mult_factor, max_input_int)};
	static constexpr uint8_t bitShifts{calc_bitshifts(max_mult_fact)};
	static constexpr calc_type mult_factor_int{calc_mult_fact_int(mult_factor, bitShifts)};

public:

	OPT_MATH_SHIFT static constexpr io_type mult(io_type input_val)
	{
		calc_type output_val = static_cast<calc_type>(input_val)*mult_factor_int;
		output_val = output_val>>bitShifts;
		return static_cast<io_type>(output_val);
	}

	OPT_MATH_SHIFT constexpr io_type operator*(io_type val) const
	{
		return mult(val);
	}
};


#endif //__cplusplus
