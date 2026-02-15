# math_bits — Fast, FPU‑free constant‑factor multiplication
math_bits provides high‑performance multiplication of integer values by constant floating‑point factors, without using hardware floating‑point instructions.
The library replaces slow runtime float operations with optimized integer arithmetic generated at compile time.

Ideal for microcontrollers and embedded systems without FPU support.

# Features
- Header‑only C++20 library
- Compile‑time generation of integer multiplier and bit‑shift parameters
- No floating‑point operations at runtime
- Deterministic execution time (ISR‑safe)
- Static unit‑testing at compile time
- Supports 8‑bit, 16‑bit, and 32‑bit integer types
- Configurable maximum input range and allowed error

# Example
```cpp
#include "math_bits.h"

// Multiply by 1.234 with max input 4095(f.ex 12bit adc), allow ±1 LSB error on result
constexpr double factor_value = 1.234;
constexpr max_input_value = 4095;
using calc_variable_type = uint32_t;
using result_variable_type = uint16_t;
constexpr uint16_t allowed_error = 1;
using factor_type = mult_bitshift<factor_value, max_input_value, result_variable_type, calc_variable_type, allowed_error>;

factor_type factor;
uint16_t value = 1000;
uint16_t result = factor * value;
```


# Template parameters
```cpp
template<
    auto multvalue,          // Floating-point multiplier (float/double/long double)
    auto max_input_value,    // Maximum allowed input
    typename io_type=uint32_t,
    typename calc_type=uint32_t,
    io_type max_error=1,     // Allowed deviation from true float result
    bool force_inlining=false
>
class mult_bitshift;
```

# Parameter meaning
| Parameter | Description |
|----------|-------------|
| `multvalue` | The constant floating‑point factor (e.g., 1.234) |
| `max_input_value` | Maximum integer input the multiplier must support |
| `io_type` | Input/output integer type |
| `calc_type` | Internal calculation type |
| `max_error` | Allowed deviation from true float result |
| `force_inlining` | Force inline version of multiplication |

# Compile‑time unit testing
Each instantiation of mult_bitshift runs a constexpr unit test verifying:
- error bounds
- integer overflow safety
- correct bit‑shift scaling
If the test fails, compilation stops with a clear static assertion.

# Performance
- No floating‑point instructions
- Only integer multiply + bit‑shift
- Constant execution time
- Suitable for real‑time control loops and ISR contexts
- Low‑power MCUs
- DSP‑like operations without an FPU

# Accuracy
The result is guaranteed to be within:
```± max_error``` of the true floating‑point multiplication.

# License
MIT
