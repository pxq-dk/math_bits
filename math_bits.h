/*
 * math_bitshifting.h
 *
 *  Created on: Oct 23, 2025
 *      Author: pxq-dk ( PxQ Technologies, https://pxq.dk )
 */

#ifndef MATH_BITS_H_
#define MATH_BITS_H_

#include <limits>
#include <cmath>

#define OPT_MATH_SHIFT __attribute__ ((optimize("-Os")))

#ifdef __cplusplus

template<auto multvalue, uint32_t max_input_value, typename io_type=uint32_t, typename calc_type=uint32_t>
class mult_bitshift
{
private:
	static constexpr calc_type calc_max_mult(auto multFact, io_type max_input_value_)
	{
		// Check whether the datatype of multvalue is allowed.
		static_assert(	(std::is_same<float,decltype(multFact)>() || \
						std::is_same<double,decltype(multFact)>() || \
						std::is_same<long double,decltype(multFact)>()),	 \
						"multvalue datatype must be either float or double!");

		calc_type maxVal = std::numeric_limits<calc_type>::max();
		decltype(multvalue) tmp_res = (decltype(multvalue))maxVal/(multFact*(decltype(multvalue))max_input_value_);
		return (calc_type)tmp_res;
	}

	static constexpr uint8_t calc_bitshifts(calc_type value)
	{
		return (uint8_t)floor(log2(value));
	}

	static constexpr calc_type calc_mult_fact_int(auto multFact, uint8_t bitshifts)
	{
		calc_type maxVal = std::numeric_limits<calc_type>::max();
		maxVal = maxVal >> ( std::numeric_limits<calc_type>::digits-bitshifts);
		calc_type mult_val = (calc_type)round((multFact*(decltype(multvalue))maxVal));
		return mult_val;
	}

	static constexpr auto mult_factor{multvalue};
	static constexpr io_type max_input_int{max_input_value};
	static constexpr calc_type max_mult_fact{calc_max_mult(mult_factor, max_input_int)};
	static constexpr uint8_t bitShifts{calc_bitshifts(max_mult_fact)};
	static constexpr calc_type mult_factor_int{calc_mult_fact_int(mult_factor, bitShifts)};

public:

	OPT_MATH_SHIFT static constexpr io_type mult(io_type input_val)
	{
		calc_type output_val = ((calc_type)input_val)*mult_factor_int;
		output_val = output_val>>bitShifts;
		return (io_type)output_val;
	}

	OPT_MATH_SHIFT constexpr io_type operator*(io_type val)
	{
		return mult(val);
	}
};


#endif //__cplusplus


#endif /* MATH_BITS_H_ */
