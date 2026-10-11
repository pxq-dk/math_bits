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
#include <numeric>

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

// Rounding by shift for a large bias, on cores that cannot add a large constant directly.
// With trade_speed_for_precision, mult() adds the bias 2^(bitShifts-1) before the shift; from this
// value on, building that constant costs more than rounding by shifting (identical results):
//   ARMv6-M (Cortex-M0/M0+):         256    (adds takes 0..255; larger needs movs + lsls)
//   ARMv8-M Baseline (Cortex-M23):   65536  (movw loads 16-bit constants in one instruction)
//   other cores:                     0 = off (add takes any power of two as an immediate)
// Predefine to override the detection (0 disables).
#ifndef MATH_BITS_SHIFT_ROUND_MIN_BIAS
    #if defined(__ARM_ARCH_6M__)
        #define MATH_BITS_SHIFT_ROUND_MIN_BIAS 256
    #elif defined(__ARM_ARCH_8M_BASE__)
        #define MATH_BITS_SHIFT_ROUND_MIN_BIAS 65536
    #else
        #define MATH_BITS_SHIFT_ROUND_MIN_BIAS 0
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
//   Cost: one extra add per mult() call (typically 1 cycle on Cortex-M0+); when calc_type
//   has no room for the bias, the same rounding is done with one extra shift and add
//   instead (no room needed); on Cortex-M0/M0+/M23 also when that is shorter
//   (MATH_BITS_SHIFT_ROUND_MIN_BIAS). With sufficient headroom, max_error = 0 will compile-pass;
//   otherwise the static
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

// An exact ratio num / den as multvalue, for scaling by a fraction without floating point:
//     using ticks_to_ms = mult_bitshift<math_bits_ratio{1000, 48000}, 0xFFFFFFFFu>;
// or, shorter, mult_bitshift_ratio<1000, 48000, 0xFFFFFFFFu> (below). mult() is then checked
// against the exact integer result floor(x * num / den) (with trade_speed_for_precision:
// floor(x * num / den + 1/2)), not against a floating-point expression. The ratio is reduced
// (1000/48000 -> 1/48); it must not reduce to an integer (den 1): integer multiplication is not
// supported, the compiler already does it optimally.
struct math_bits_ratio
{
    uint64_t num;
    uint64_t den;
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
        // Non-floating multvalue is rejected by mult_bitshift's own static_assert; return a
        // harmless value here so that clear message is not buried under follow-on errors.
        if constexpr (!std::is_floating_point_v<T>) return { 1, 0 };
        constexpr int D = std::numeric_limits<T>::digits; // 24 (float), 53 (double), 64 (x86 long double)
        static_assert(D <= 64, "multvalue's floating-point type has more than 64 mantissa bits (e.g. 128-bit long double); use double");
        T top = 1;
        for (int i = 0; i < D; ++i) top *= 2;              // 2^D, exact
        int e = 0;
        while (v >= top)    { v /= 2; ++e; }
        while (v < top / 2) { v *= 2; --e; }
        return { static_cast<uint64_t>(v), e };
    }

    // The exact multiplier as a positive value num / den * 2^exp (den >= 1). A floating-point
    // multvalue is its exact binary value mant * 2^exp (den = 1, see decompose).
    struct rational
    {
        uint64_t num;
        uint64_t den;
        int      exp;
    };

    // Quotient and remainder of n / d, for d >= 1 (bit by bit; compile time only).
    struct u128_divmod
    {
        u128     quot;
        uint64_t rem;
    };

    constexpr u128_divmod divmod(const u128& n, uint64_t d)
    {
        if (n.hi == 0) return { u128(n.lo / d), n.lo % d };
        if ((d >> 32) == 0)                                               // base-2^32 long division:
        {                                                                 // r < d < 2^32, so r * 2^32 + digit fits
            const uint64_t digit[4] = { n.hi >> 32, n.hi & 0xFFFFFFFFu, n.lo >> 32, n.lo & 0xFFFFFFFFu };
            uint64_t qd[4] = {}, r = 0;
            for (int i = 0; i < 4; ++i)
            {
                const uint64_t cur = (r << 32) | digit[i];
                qd[i] = cur / d;
                r = cur % d;
            }
            return { u128((qd[0] << 32) | qd[1], (qd[2] << 32) | qd[3]), r };
        }
        u128 q;
        uint64_t r = 0;
        for (int i = n.bit_width() - 1; i >= 0; --i)
        {
            const bool carry = (r >> 63) != 0;                            // 2 * r overflows 64 bits
            r = (r << 1) | (((i >= 64) ? (n.hi >> (i - 64)) : (n.lo >> i)) & 1u);
            q = q << 1;
            if (carry || r >= d) { r -= d; q = q + u128(1); }           // exact mod 2^64: 2 * r < 2 * d
        }
        return { q, r };
    }

    // Inverse of a modulo m: a * inv == 1 (mod m), for gcd(a, m) == 1 and 2 <= m < 2^62 (extended
    // Euclid; the coefficients stay below m in magnitude, so they fit int64_t).
    constexpr uint64_t mod_inverse(uint64_t a, uint64_t m)
    {
        int64_t t = 0, nt = 1;
        uint64_t r = m, nr = a % m;
        while (nr != 0)
        {
            const uint64_t q = r / nr;
            const int64_t tt = t - static_cast<int64_t>(q) * nt;
            t = nt; nt = tt;
            const uint64_t rr = r - q * nr;
            r = nr; nr = rr;
        }
        return t < 0 ? static_cast<uint64_t>(t + static_cast<int64_t>(m)) : static_cast<uint64_t>(t);
    }

    // Sign of (a * b * 2^da) - (c * d * 2^dc), exact, for a, c < 2^128, b, d < 2^64 and any shifts
    // (also negative). The products have at most 192 bits; after taking out the common power of two
    // only one side is shifted, and only when both sides have the same bit width, so 256 bits suffice.
    constexpr int cmp_scaled_products(const u128& a, uint64_t b, int da, const u128& c, uint64_t d, int dc)
    {
        auto product = [](const u128& x, uint64_t y, uint64_t (&w)[4])
        {
            const u128 lo = mul64(x.lo, y), hi = mul64(x.hi, y);
            const uint64_t w1 = lo.hi + hi.lo;
            w[0] = lo.lo; w[1] = w1; w[2] = hi.hi + (w1 < lo.hi ? 1u : 0u); w[3] = 0;
        };
        auto width = [](const uint64_t (&w)[4])
        {
            for (int i = 3; i >= 0; --i)
                if (w[i]) return 64 * i + static_cast<int>(std::bit_width(w[i]));
            return 0;
        };
        auto shift_left = [](uint64_t (&w)[4], int n)                     // result known to fit
        {
            const int ws = n / 64, bs = n % 64;
            for (int i = 3; i >= 0; --i)
            {
                uint64_t v = (i - ws >= 0) ? (w[i - ws] << bs) : 0u;
                if (bs && i - ws - 1 >= 0) v |= w[i - ws - 1] >> (64 - bs);
                w[i] = v;
            }
        };
        uint64_t L[4], R[4];
        product(a, b, L);
        product(c, d, R);
        const int wl = width(L), wr = width(R);
        if (wl == 0 || wr == 0) return wl ? 1 : (wr ? -1 : 0);
        const int m = da < dc ? da : dc;
        da -= m;
        dc -= m;
        if (wl + da != wr + dc) return (wl + da > wr + dc) ? 1 : -1;
        if (da)      shift_left(L, da);
        else if (dc) shift_left(R, dc);
        for (int i = 3; i >= 0; --i)
            if (L[i] != R[i]) return L[i] > R[i] ? 1 : -1;
        return 0;
    }

    // q value for cmp_mul_plus_pow2: no 2^q term.
    inline constexpr int no_pow2_term = std::numeric_limits<int>::min();

    // Sign of (a * b + 2^q) - (c * 2^d), exact, for a < 2^128, b < 2^64, c < 2^128, 0 <= d, q < 192.
    // q < 0 stands for a term strictly between 0 and 1; q == no_pow2_term for no term at all.
    // Uses 256-bit intermediates, so nothing can overflow.
    constexpr int cmp_mul_plus_pow2(const u128& a, uint64_t b, int q, const u128& c, int d)
    {
        const u128 lo = mul64(a.lo, b), hi = mul64(a.hi, b);
        const uint64_t w1 = lo.hi + hi.lo;
        uint64_t L[4] = { lo.lo, w1, hi.hi + (w1 < lo.hi ? 1u : 0u), 0 };       // a*b
        if (q >= 0)                                                               // + 2^q
        {
            int i = q / 64;
            uint64_t add = uint64_t{1} << (q % 64);
            while (i < 4 && add) { const uint64_t s = L[i] + add; add = (s < L[i]) ? 1u : 0u; L[i] = s; ++i; }
        }
        uint64_t R[4] = { 0, 0, 0, 0 };                                           // c * 2^d
        if (c.bit_width())
        {
            if (c.bit_width() + d > 256) return -1;                               // R >= 2^256 > L
            const uint64_t cw[2] = { c.lo, c.hi };
            const int ws = d / 64, bs = d % 64;
            for (int i = 0; i < 2; ++i)
            {
                if (i + ws < 4)                  R[i + ws]     |= cw[i] << bs;
                if (bs && i + ws + 1 < 4)        R[i + ws + 1] |= cw[i] >> (64 - bs);
            }
        }
        for (int i = 3; i >= 0; --i)
            if (L[i] != R[i]) return L[i] > R[i] ? 1 : -1;
        return (q < 0 && q != no_pow2_term) ? 1 : 0;                             // equal integer parts
    }

    // ---- IEEE 754 emulation (round to nearest, ties to even) ----------------------------------
    // Used for the self-test reference: what the floating-point expression would give.

    // A value mant * 2^exp with a 128-bit mantissa.
    struct dyadic128
    {
        u128 mant;
        int  exp;
    };

    // Round mant * 2^exp to P significant bits, ties to even (the IEEE default rounding).
    constexpr dyadic128 round_to_precision(const u128& mant, int exp, int P)
    {
        const int bw = mant.bit_width();
        if (bw <= P) return { mant, exp };                      // already representable
        const int shift = bw - P;
        u128 q = mant >> shift;
        const u128 rem  = mant - (q << shift);
        const u128 half = u128(1) << (shift - 1);
        if (half < rem || (rem == half && (q.lo & 1u))) q = q + u128(1);
        if (q.bit_width() > P) return { q >> 1, exp + shift + 1 }; // carry to 2^P: exact, even
        return { q, exp + shift };
    }

    // floor of the IEEE expression  (T)x * f        (bias == false)
    //                          or   (T)x * f + 0.5  (bias == true)
    // evaluated in a floating-point type with P mantissa bits, every step rounded to nearest-even:
    // the integer-to-float conversion of x, the product, and the bias addition. f = f.mant * 2^f.exp
    // must itself be a value of that type (as returned by decompose()). Result must fit 128 bits.
    constexpr u128 ieee_mul_floor_full(uint64_t x, const dyadic& f, int P, bool bias)
    {
        const dyadic128 xr = round_to_precision(u128(x), 0, P);           // (T)x
        const dyadic128 pr = round_to_precision(mul64(xr.mant.lo, f.mant), // xr.mant < 2^P <= 2^64
                                                xr.exp + f.exp, P);       // (T)x * f
        dyadic128 r = pr;
        if (bias)                                                         // + 0.5, rounded again
        {
            if (pr.exp < -120) return u128(0);                            // value < 2^-56: +0.5 -> 0.5
            const int e = pr.exp < -1 ? pr.exp : -1;                      // common exponent with 0.5
            const u128 sum = (pr.mant << (pr.exp - e)) + (u128(1) << (-1 - e));
            r = round_to_precision(sum, e, P);
        }
        return r.exp >= 0 ? (r.mant << r.exp) : (r.mant >> -r.exp);       // floor (conversion to int)
    }

    // Same result as ieee_mul_floor_full, but cheap in the common case (it runs for every
    // self-test sample). IEEE rounding is monotonic and integers below 2^P are representable, so
    // rounding can only change floor() by pushing a value that lies just BELOW an integer up onto
    // it. So: take the exact value (product, plus 0.5 with the bias), and if it is further below
    // the next integer than the rounding steps could move it, its exact floor is the answer.
    // Otherwise — and for x >= 2^P (the conversion rounds) or very large results — emulate fully.
    constexpr u128 ieee_mul_floor(uint64_t x, const dyadic& f, int P, bool bias)
    {
        if (static_cast<int>(std::bit_width(x)) > P || f.exp < -120) return ieee_mul_floor_full(x, f, P, bias);
        const u128 p = mul64(x, f.mant);                                  // exact x * f = p * 2^f.exp
        if (f.exp >= 0)                                                   // integer product: exact if it
        {                                                                 // fits, and +0.5 needs one more bit
            const int need = p.bit_width() + f.exp + (bias ? 1 : 0);
            return (need <= P) ? (p << f.exp) : ieee_mul_floor_full(x, f, P, bias);
        }
        const int k = -f.exp;                                             // value = v / 2^k
        const int step_p = p.bit_width() - P;                             // product rounding step 2^step_p
        u128 v = p;
        int step_s = 0;
        if (bias)
        {
            v = p + (u128(1) << (k - 1));                                 // + 0.5, exact
            step_s = v.bit_width() + 1 - P;                               // sum rounding step (bound)
        }
        const int step = step_p > step_s ? step_p : step_s;
        const u128 q = v >> k;                                            // exact floor
        if (step <= 0) return q;                                          // nothing is rounded
        const u128 dist = ((q + u128(1)) << k) - v;                       // distance to next integer
        // Each rounding moves the value by at most half its step; both together by less than 2^step.
        if (step < k && (u128(1) << step) < dist) return q;
        return ieee_mul_floor_full(x, f, P, bias);
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
    // once (the sweep step is then exactly 1); otherwise the range is sampled.
    static constexpr std::size_t calc_loop_iterations()
    {
    	constexpr uint64_t cap = test_in_depth ? max_loop_iterations : min_loop_iterations;
    	// Compare before adding 1: max_input_int + 1 would wrap to 0 for a uint64_t max input.
    	constexpr uint64_t max_in = static_cast<uint64_t>(MultType::max_input_int);

    	return static_cast<std::size_t>(max_in < cap ? max_in + 1 : cap);
    }

    static constexpr std::size_t loop_iterations = calc_loop_iterations();

    static constexpr float_type mult_factor = MultType::mult_factor;        // Floating-point multiplier

    // Reference result for `input`. For a ratio P/Q the exact integer result floor(x * P / Q)
    // (with trade_speed_for_precision floor(x * P / Q + 1/2)). For a floating-point multvalue,
    // what the floating-point expression gives, evaluated in
    // multvalue's own type T with IEEE rounding (to nearest, ties to even) at every step:
    //   truncation (default):          (out_type)( (T)input * multvalue )
    //   trade_speed_for_precision:     (out_type)( (T)input * multvalue + (T)0.5 )
    // So mult() is checked against the code it replaces — e.g. 10 * 0.7 is 7 there, although
    // the stored double 0.69999999999999995559 times 10 is just below 7. Emulated exactly in
    // 128-bit integers (math_bits_detail::ieee_mul_floor), so it does not depend on the host's or
    // target's floating-point unit. reference_bias is only used for the floating-point starting
    // guess in level_start().
    static constexpr long double reference_bias = MultType::trade_speed_for_precision ? 0.5L : 0.0L;
    static constexpr int reference_precision = std::numeric_limits<float_type>::digits;
    // multvalue = mant * 2^exp as the IEEE emulation takes it (f_value.den is 1 for a floating-point multvalue).
    static constexpr math_bits_detail::dyadic f_ieee = { MultType::f_value.num, MultType::f_value.exp };

    // Per-configuration constants for a cheap reference (it runs for every self-test sample, and
    // GCC limits the operations of a constant evaluation). value = v / 2^ref_k, with v = x * mant
    // (+ 2^(ref_k-1) for the bias). ref_step bounds the IEEE rounding steps for every x <= max_input
    // (in units of 2^-ref_k): a value can only be rounded up onto the next integer when its
    // fractional bits from ref_step upward are all ones; only then is the full emulation needed.
    static constexpr int ref_k = -f_ieee.exp;
    static constexpr bool ref_has_bias = MultType::trade_speed_for_precision;
    static constexpr int calc_ref_step()
    {
    	using math_bits_detail::u128;
    	if (ref_k <= 0 || ref_k >= 120) return 0;
    	const u128 pmax = math_bits_detail::mul64(static_cast<uint64_t>(MultType::max_input_int), f_ieee.mant);
    	const int sp = pmax.bit_width() - reference_precision;
    	const int ss = ref_has_bias ? (pmax + (u128(1) << (ref_k - 1))).bit_width() + 1 - reference_precision : sp;
    	return sp > ss ? sp : ss;
    }
    static constexpr int ref_step = calc_ref_step();
    static constexpr bool ref_fast = ref_k > 0 && ref_k < 120 && ref_step < ref_k
    	&& static_cast<int>(std::bit_width(static_cast<uint64_t>(MultType::max_input_int))) <= reference_precision;

    static constexpr out_type reference(io_type input)
    {
    	using math_bits_detail::u128;
    	if constexpr (MultType::is_ratio)
    	{
    		// Exact: floor(x * P / Q), or with the bias floor(x * P / Q + 1/2) = q + (2r >= Q).
    		constexpr math_bits_detail::rational f = MultType::f_value;
    		const math_bits_detail::u128_divmod qr = math_bits_detail::divmod(
    			math_bits_detail::mul64(static_cast<uint64_t>(input), f.num), f.den);
    		if constexpr (ref_has_bias)
    			return static_cast<out_type>(qr.rem >= f.den - qr.rem ? qr.quot.lo + 1u : qr.quot.lo);
    		else
    			return static_cast<out_type>(qr.quot.lo);
    	}
    	else if constexpr (ref_fast)
    	{
    		u128 v = math_bits_detail::mul64(static_cast<uint64_t>(input), f_ieee.mant);
    		if constexpr (ref_has_bias) v = v + (u128(1) << (ref_k - 1));
    		const u128 q = v >> ref_k;                          // exact floor
    		if constexpr (ref_step > 0 && !reference_exact)     // exact reference: nothing is rounded
    		{
    			// Rounding can lift the value onto q + 1 only if its fractional bits from ref_step up
    			// are all ones (within 2^ref_step of the next integer): then emulate fully.
    			bool near;
    			if constexpr (ref_k <= 64)                       // fraction fits 64 bits: cheap check
    			{
    				constexpr uint64_t frac_mask = (ref_k == 64) ? ~uint64_t{0} : ((uint64_t{1} << ref_k) - 1);
    				constexpr uint64_t near_bits = (frac_mask >> ref_step);
    				near = ((v.lo & frac_mask) >> ref_step) == near_bits;
    			}
    			else                                             // fraction spans both 64-bit words
    			{
    				constexpr uint64_t hi_mask = (uint64_t{1} << (ref_k - 64)) - 1;   // ref_k < 120
    				if constexpr (ref_step < 64)
    					near = (v.lo >> ref_step) == (~uint64_t{0} >> ref_step) && (v.hi & hi_mask) == hi_mask;
    				else
    					near = ((v.hi & hi_mask) >> (ref_step - 64)) == (hi_mask >> (ref_step - 64));
    			}
    			if (near)
    				return static_cast<out_type>(math_bits_detail::ieee_mul_floor_full(static_cast<uint64_t>(input),
    					f_ieee, reference_precision, ref_has_bias).lo);
    		}
    		return static_cast<out_type>(q.lo);
    	}
    	else
    	{
    		return static_cast<out_type>(math_bits_detail::ieee_mul_floor(static_cast<uint64_t>(input),
    			f_ieee, reference_precision, ref_has_bias).lo);
    	}
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

    // Broad sweep: loop_iterations evenly spaced inputs over [0, max_input_int] (every input when
    // the range fits under the cap). Spacing is computed in double; the last sample is pinned to
    // max_input_int exactly, since i * step can land just below it (missing the input with the
    // largest error) and, for a 64-bit input, `stop` itself can round up past it (casting a
    // double >= 2^64 to uint64_t is UB).
    static constexpr bool run_test()
    {
    	constexpr std::size_t N = loop_iterations;          // >= 2, since max_input_int >= 1
    	constexpr double stop = MultType::max_input_int;
    	constexpr double step = stop / (N - 1);

    	for (std::size_t i = 0; i < N; ++i)
    	{
    		const double value = i * step;
    		const io_type input = (i == N - 1 || value >= stop) ? MultType::max_input_int : static_cast<io_type>(value);
    		if (!number_ok(input)) return false;
    	}
    	return true;
    }

    // ---- Error analysis + targeted gray-zone check ---------------------------------------
    //
    // mult(x) = floor(x*M/2^s + b) and reference(x) = floor(R(x)), where R(x) is the IEEE result
    // of x*f + b. They differ internally by delta(x) = x*e - rho(x), with e = M/2^s - f (the
    // quantization error of the integer factor) and rho(x) = R(x) - (x*f + b) (the IEEE rounding of
    // the reference; 0 when every product is exactly representable). |rho(x)| <= rho_bound, so
    // the output error at x is at most ceil(x*|e| + rho_bound), and with
    // worst_deviation = max_input * |e| + rho_bound:
    //   - worst_deviation <= max_error  -> proven OK for every input (no check needed)
    //   - otherwise (gray zone)         -> only x with x*|e| + rho_bound > max_error can exceed it.
    //     Within one output level n (= reference(x), constant within the level and non-decreasing
    //     in x, since IEEE rounding is monotonic) mult(x) is non-decreasing, so the worst overshoot
    //     is at the level's largest x and the worst undershoot at its smallest x. Checking both
    //     ends of each level is exact. (Max_input failing is a proven failure: endpoint check.)
    //
    // Not applied when bitShifts == 0 with trade_speed_for_precision: round_bias is then 0
    // while the reference still adds 0.5, so the shared-bias premise does not hold.
    static constexpr bool analysis_applies =
    	!(MultType::trade_speed_for_precision && MultType::bitShifts == 0);

    // Informational only (approximate, long double): handy for inspecting a configuration.
    // The decisions below use the exact values (exact_quant_error).
    static constexpr long double quant_error =
    	static_cast<long double>(MultType::mult_factor_int)
    		/ static_cast<long double>(static_cast<uint64_t>(1) << MultType::bitShifts)
    	- static_cast<long double>(mult_factor);
    static constexpr long double abs_quant_error = quant_error < 0 ? -quant_error : quant_error;
    static constexpr long double worst_deviation =
    	static_cast<long double>(MultType::max_input_int) * abs_quant_error;

    // Exact quantization error, from f = n/d * 2^E (MultType::f_value) and M = mult_factor_int at
    // shift s:  |e| = |M/2^s - f| = num / (den * 2^den_exp), with t = E + s:
    //   t >= 0:  num = |M*d - n*2^t|,      den = d, den_exp = s
    //   t <  0:  num = |M*d*2^-t - n|,     den = d, den_exp = -E
    // For a floating-point multvalue (d = 1), e is exactly 0 when t >= 0 (M is then n * 2^t).
    struct exact_quant_error
    {
    	math_bits_detail::u128 num;   // 0 when the multiplier is exact
    	uint64_t den;
    	int  den_exp;
    	bool high;                    // e >= 0: mult() never runs low
    };

    static constexpr exact_quant_error calc_exact_quant_error()
    {
    	using math_bits_detail::u128;
    	constexpr math_bits_detail::rational f = MultType::f_value;
    	const int t = f.exp + MultType::bitShifts;
    	const u128 Md = math_bits_detail::mul64(static_cast<uint64_t>(MultType::mult_factor_int), f.den);
    	// An unrepresentable error is treated as huge (nothing proven, everything checked).
    	constexpr exact_quant_error huge = { u128(~uint64_t{0}, ~uint64_t{0}), f.den, -f.exp, false };
    	if (t >= 0)
    	{
    		if (f.den == 1) return { u128(0), 1, 0, true };
    		if (64 + t > 128) return huge;
    		const u128 n = u128(f.num) << t;
    		return { (n < Md) ? Md - n : n - Md, f.den, MultType::bitShifts, !(Md < n) };
    	}
    	const int k = -t;
    	// For d = 1, M * 2^k is within 2^(k-1) of n < 2^64 and k <= 127, so it fits; guard anyway.
    	if (Md.bit_width() + k > 128) return huge;
    	const u128 a = Md << k;
    	const u128 n = f.num;
    	return { (n < a) ? a - n : n - a, f.den, -f.exp, !(a < n) };
    }

    static constexpr exact_quant_error exact_error = calc_exact_quant_error();

    // ---- IEEE rounding of the reference: rho ----
    // floor(max_input * f), exact.
    static constexpr math_bits_detail::u128 calc_max_product_floor()
    {
    	const math_bits_detail::dyadic f = f_ieee;
    	const math_bits_detail::u128 p = math_bits_detail::mul64(static_cast<uint64_t>(MultType::max_input_int), f.mant);
    	return f.exp >= 0 ? (p << f.exp) : (p >> -f.exp);
    }

    // True when the reference involves no rounding at all for any x <= max_input, i.e. rho == 0:
    // the conversion (T)x is exact, every product x * f fits the mantissa, and (with the bias) so
    // does product + 0.5.
    static constexpr bool calc_reference_exact()
    {
    	if constexpr (MultType::is_ratio) return true;                  // exact integer reference
    	const math_bits_detail::dyadic f = f_ieee;
    	const int P = reference_precision;
    	const int bw_in = static_cast<int>(std::bit_width(static_cast<uint64_t>(MultType::max_input_int)));
    	uint64_t m_odd = f.mant;
    	int tz = 0;
    	while (m_odd && !(m_odd & 1u)) { m_odd >>= 1; ++tz; }
    	const int bw_m = static_cast<int>(std::bit_width(m_odd));
    	if (bw_in > P || bw_in + bw_m > P) return false;                 // conversion or product rounds
    	if (!MultType::trade_speed_for_precision) return true;
    	const int lowest = f.exp + tz;                                    // weight of the product's lowest bit
    	if (lowest >= -1) return calc_max_product_floor().bit_width() + 1 <= P;   // + 2^-1 bit
    	return bw_in + bw_m + 1 <= P;                                     // + 0.5 inside range, + carry
    }
    static constexpr bool reference_exact = calc_reference_exact();

    // Otherwise |rho(x)| < 2^rho_exp for every x <= max_input: with every value involved below
    // 2^E (E = bit width of floor(max_input * f) + 2), each IEEE rounding step is at most half an
    // ulp, 2^(E-P-1): the product and the bias addition one each, and the conversion of x at most
    // 2^(E-P) after multiplying by f. Together below 2^(E-P+1).
    static constexpr int rho_exp =
    	(calc_max_product_floor() + math_bits_detail::u128(2)).bit_width() - reference_precision + 1;

    // Sign of (x * |e| + rho_bound) - k, exact (rho_bound = 0 when the reference is exact,
    // else 2^rho_exp). Everything is scaled by den * 2^den_exp to stay in integers; rho only occurs
    // with the IEEE reference, where den = 1.
    static constexpr int cmp_deviation(uint64_t x, uint64_t k)
    {
    	static_assert(reference_exact || exact_error.den == 1, "rho term requires den == 1");
    	constexpr int q = reference_exact ? math_bits_detail::no_pow2_term : rho_exp + exact_error.den_exp;
    	return math_bits_detail::cmp_mul_plus_pow2(exact_error.num, x, q, math_bits_detail::mul64(k, exact_error.den),
    	                                           exact_error.den_exp);
    }

    // Ratio only: proven exact for every input. x * P / Q has fractional parts in steps of 1/Q
    // (x * P / Q + 1/2 in steps of 1/(2Q)), so a multiplier that runs high by less than one step
    // never reaches the next integer, and mult(x) equals the reference for every x <= max_input:
    //   0 <= max_input * e < 1/Q (1/(2Q))  <=>  e >= 0  and  max_input * num < 2^s (2^(s-1)),
    // with e = num / (Q * 2^s) (exact_quant_error). The classic division-by-a-constant argument.
    static constexpr bool calc_ratio_exact()
    {
    	if constexpr (!MultType::is_ratio || !analysis_applies) return false;
    	else
    	{
    		constexpr int s = MultType::bitShifts - (MultType::trade_speed_for_precision ? 1 : 0);
    		return exact_error.high && exact_error.den_exp == MultType::bitShifts
    			&& math_bits_detail::cmp_scaled_products(exact_error.num, static_cast<uint64_t>(MultType::max_input_int), 0,
    			                                         math_bits_detail::u128(1), 1, s) < 0;
    	}
    }
    static constexpr bool ratio_exact = calc_ratio_exact();

    // Proven OK: max_input * |e| + rho_bound <= max_error, so ceil(worst deviation) <= max_error;
    // or, for a ratio, proven exact (ratio_exact).
    static constexpr bool proven_ok = analysis_applies
    	&& (ratio_exact
    	    || cmp_deviation(static_cast<uint64_t>(MultType::max_input_int), static_cast<uint64_t>(max_deviation)) <= 0);

    // Largest input x <= max_input with x * |e| + rho_bound <= max_error: inputs up to here cannot
    // exceed max_error (0 when even x = 0 is not provable; x = 0 always gives 0 on both sides).
    // Binary search with exact comparisons (at most 64 steps).
    static constexpr uint64_t calc_x_safe()
    {
    	uint64_t lo = 0, hi = static_cast<uint64_t>(MultType::max_input_int);
    	while (lo < hi)
    	{
    		const uint64_t mid = lo + (hi - lo) / 2 + 1;
    		if (cmp_deviation(mid, static_cast<uint64_t>(max_deviation)) <= 0) lo = mid;
    		else                                                               hi = mid - 1;
    	}
    	return lo;
    }

    // floor(max_input * |e| + rho_bound): the proven worst-case bound, rounded down. Exact, by
    // binary search (the bound is below max_input + 1). Used by the multiplier selection.
    static constexpr uint64_t worst_deviation_floor()
    {
    	constexpr uint64_t x_max = static_cast<uint64_t>(MultType::max_input_int);
    	uint64_t lo = 0, hi = x_max;
    	while (lo < hi)
    	{
    		const uint64_t mid = lo + (hi - lo) / 2 + 1;
    		if (cmp_deviation(x_max, mid) >= 0) lo = mid;
    		else                                hi = mid - 1;
    	}
    	return lo;
    }

    // True when the bound max_input * |e| + rho_bound is exactly an integer (floor == ceil).
    static constexpr bool worst_deviation_is_integer()
    {
    	return cmp_deviation(static_cast<uint64_t>(MultType::max_input_int), worst_deviation_floor()) == 0;
    }

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
    	if constexpr (MultType::is_ratio)
    	{
    		// Exact. floor(x*P/Q) >= n  <=>  x*P >= n*Q;  with the bias floor(x*P/Q + 1/2) >= n
    		// <=>  2*x*P >= (2n-1)*Q  <=>  x*P >= (n-1)*Q + ceil(Q/2) (n >= 1). So x = ceil(V / P).
    		// No overflow: n < 2^64 and Q < 2^64, so V + P - 1 < 2^128.
    		using math_bits_detail::u128;
    		constexpr math_bits_detail::rational f = MultType::f_value;
    		if (n == 0) return 0;
    		const u128 V = ref_has_bias ? math_bits_detail::mul64(n - 1, f.den) + u128(f.den - f.den / 2)
    		                             : math_bits_detail::mul64(n, f.den);
    		const u128 x = math_bits_detail::divmod(V + u128(f.num - 1), f.num).quot;
    		return (x.hi != 0 || x.lo > x_max) ? x_max : x.lo;
    	}
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

    // ---- Ratio: exact check by residues ----
    // For a ratio P/Q, mult(x) = floor(x*P/Q + x*e [+ 1/2]) and reference(x) = floor(x*P/Q [+ 1/2]).
    // The fractional part of x*P/Q [+ 1/2] is v/S, where v depends only on the residue r = x*P mod Q
    // (S = Q and v = r; with the bias S = 2Q and v = (2r + Q) mod 2Q), and x*e grows with x. So for
    // each residue the worst input is the largest x <= max_input with that residue, and only
    // residues whose fractional part is close enough to the next integer (e >= 0) or to 0 (e < 0)
    // can fail at all (k = max_error):
    //   e >= 0:  error >= k+1   needs  v >= S*(k+1) - S*max_input*e
    //   e <  0:  error <= -(k+1) needs  v <  S*max_input*|e| - S*k
    // Checking the worst input of each such residue (found with the inverse of P mod Q) is therefore
    // exact for every input <= max_input, without sampling. Used when at most residue_budget
    // residues qualify; otherwise the level walk below runs as for a floating-point multvalue.
    struct residue_plan
    {
    	bool     applies;              // false: use the level walk
    	uint64_t lo[2], hi[2];        // candidate residues [lo[i], hi[i]] (empty when lo > hi)
    };
    static constexpr uint64_t residue_budget = max_loop_iterations;

    static constexpr residue_plan calc_residue_plan()
    {
    	constexpr residue_plan walk = { false, { 1, 1 }, { 0, 0 } };
    	constexpr residue_plan none = { true,  { 1, 1 }, { 0, 0 } };    // no residue can fail
    	if constexpr (!MultType::is_ratio) return walk;
    	else
    	{
    		using math_bits_detail::u128;
    		constexpr uint64_t Q = MultType::f_value.den;
    		constexpr int s = MultType::bitShifts;
    		if (Q >= (uint64_t{1} << 61) || exact_error.den_exp != s || exact_error.num.hi != 0) return walk;
    		const uint64_t S = ref_has_bias ? 2 * Q : Q;
    		// S * max_input * |e| = T / 2^s  (|e| = num / (Q * 2^s))
    		u128 T = math_bits_detail::mul64(exact_error.num.lo, static_cast<uint64_t>(MultType::max_input_int));
    		if (ref_has_bias)
    		{
    			if (T.hi >> 63) return walk;
    			T = T << 1;
    		}
    		const u128 w_floor = T >> s;
    		const u128 w_ceil  = ((w_floor << s) == T) ? w_floor : w_floor + u128(1);
    		const u128 Sk = math_bits_detail::mul64(S, static_cast<uint64_t>(max_deviation));
    		uint64_t v_lo, v_hi;                                         // candidate v in [v_lo, v_hi]
    		if (exact_error.high)
    		{
    			const u128 Sk1 = Sk + u128(S);
    			const u128 V = (w_floor < Sk1) ? Sk1 - w_floor : u128(0);
    			if (V.hi != 0 || V.lo >= S) return none;
    			v_lo = V.lo;
    			v_hi = S - 1;
    		}
    		else
    		{
    			if (!(Sk < w_ceil)) return none;
    			const u128 V = w_ceil - Sk;                              // v < V
    			v_lo = 0;
    			v_hi = (V.hi != 0 || V.lo > S) ? S - 1 : V.lo - 1;
    		}
    		residue_plan p = none;
    		if constexpr (!ref_has_bias)
    		{
    			p.lo[0] = v_lo;
    			p.hi[0] = v_hi;
    		}
    		else
    		{
    			const uint64_t h = (Q + 1) / 2;                          // r < h: v = 2r + Q, else v = 2r - Q
    			if (v_hi >= Q)
    			{
    				const uint64_t hi = (v_hi - Q) / 2;
    				p.lo[0] = (v_lo > Q) ? (v_lo - Q + 1) / 2 : 0;
    				p.hi[0] = (hi < h - 1) ? hi : h - 1;
    			}
    			const uint64_t lo = (v_lo + Q + 1) / 2, hi = (v_hi + Q) / 2;
    			p.lo[1] = (lo > h) ? lo : h;
    			p.hi[1] = (hi < Q - 1) ? hi : Q - 1;
    		}
    		uint64_t count = 0;
    		for (int i = 0; i < 2; ++i)
    			if (p.lo[i] <= p.hi[i]) count += p.hi[i] - p.lo[i] + 1;
    		return (count > residue_budget) ? walk : p;
    	}
    }
    static constexpr residue_plan residue = calc_residue_plan();

    // Check the worst input of every candidate residue (see calc_residue_plan).
    static constexpr bool residue_ok()
    {
    	constexpr uint64_t Q = MultType::f_value.den, x_max = MultType::max_input_int;
    	constexpr uint64_t p_inv = math_bits_detail::mod_inverse(MultType::f_value.num % Q, Q);
    	for (int i = 0; i < 2; ++i)
    	{
    		if (residue.lo[i] > residue.hi[i]) continue;
    		for (uint64_t r = residue.lo[i]; r <= residue.hi[i]; ++r)
    		{
    			const uint64_t x0 = math_bits_detail::divmod(math_bits_detail::mul64(r, p_inv), Q).rem;   // x0 * P == r (mod Q)
    			if (x0 > x_max) continue;
    			if (!number_ok(static_cast<io_type>(x0 + (x_max - x0) / Q * Q))) return false;
    		}
    	}
    	return true;
    }

    static constexpr bool gray_zone_ok()
    {
    	if constexpr (!analysis_applies || proven_ok) return true;
    	else if constexpr (residue.applies) return residue_ok();
    	else
    	{
    		constexpr uint64_t x_max = MultType::max_input_int;
    		// Inputs with x*|e| <= max_error cannot violate (exact threshold).
    		constexpr uint64_t x_lo = calc_x_safe();

    		// Check both ends of each level (largest x: worst overshoot, smallest x: worst
    		// undershoot), walking down from max_input. With an exact reference the error has the
    		// sign of e, so only one end can fail; the other end's check then always passes.
    		uint64_t checked = 0;                        // levels checked
    		uint64_t x = x_max;
    		while (checked < gray_zone_budget && x >= x_lo)
    		{
    			if (!number_ok(static_cast<io_type>(x))) return false;
    			const uint64_t start = level_start(reference(static_cast<io_type>(x)));
    			if (start != x && !number_ok(static_cast<io_type>(start))) return false;
    			++checked;
    			if (start == 0 || start - 1 < x_lo) break;
    			x = start - 1;
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
    // multvalue is an exact ratio (math_bits_ratio) or a floating-point value.
    static constexpr bool is_ratio = std::is_same_v<std::remove_cv_t<decltype(multvalue)>, math_bits_ratio>;
    // Type of the multiplier (float, double, or long double); long double for a ratio, where it
    // only types the informational mult_factor.
    using float_type = std::conditional_t<is_ratio, long double, std::remove_cv_t<decltype(multvalue)>>;
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

    // The exact multiplier, f = f_value.num / f_value.den * 2^f_value.exp: a ratio reduced to lowest
    // terms (exp = 0), or the exact binary value of a floating-point multvalue (den = 1). The parameter
    // derivation and the error analysis below work on it in exact integer arithmetic
    // (math_bits_detail), so no long double rounding is involved.
    static constexpr math_bits_detail::rational f_value = []
    {
        if constexpr (is_ratio)
        {
            // An invalid ratio gets a harmless value here; calc_bitshifts reports it.
            if (multvalue.num == 0 || multvalue.den == 0) return math_bits_detail::rational{ 1, 1, 0 };
            const uint64_t g = std::gcd(multvalue.num, multvalue.den);
            return math_bits_detail::rational{ multvalue.num / g, multvalue.den / g, 0 };
        }
        else
        {
            const math_bits_detail::dyadic d = math_bits_detail::decompose(multvalue);
            return math_bits_detail::rational{ d.mant, 1, d.exp };
        }
    }();

    // Sign of (f * max_input_value * 2^sh) - n, evaluated exactly.
    static constexpr int cmp_product(int sh, math_bits_detail::u128 n)
    {
        return math_bits_detail::cmp_scaled_products(f_value.num, static_cast<uint64_t>(max_input_value),
                                                     f_value.exp + sh, n, f_value.den, 0);
    }

    // Validate the parameters, then return the shift: the largest s < digits(calc_type) with
    // multvalue * max_input_value * 2^s <= max(calc_type), so the multiplier gets every bit of
    // calc_type that the input range leaves free.
    static constexpr uint8_t calc_bitshifts()
    {
        // Validate template parameters at compile-time
        if constexpr (is_ratio)
        {
            static_assert(multvalue.num != 0 && multvalue.den != 0, "math_bits_ratio: num and den must not be zero!");
            static_assert(f_value.den != 1, "math_bits_ratio reduces to an integer: integer multiplication is not supported (the compiler already does it optimally)");
        }
        else
        {
            static_assert(std::is_floating_point_v<float_type>, "multvalue must be float, double, long double or math_bits_ratio");
            static_assert(multvalue > 0, "multvalue must not be negative or zero!");
        }
        static_assert(std::is_unsigned_v<io_type>, "io_type must be an unsigned integer type");
        static_assert(std::is_unsigned_v<calc_type>, "calc_type must be an unsigned integer type");
        static_assert(std::is_unsigned_v<decltype(max_input_value)>, "max_input_value must be an unsigned integer type");

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

    // f * 2^bitShifts, exactly as 128-bit values: rounded down, and rounded half-up (nearest, the
    // default). The nearest one fits calc_type, since f * 2^bitShifts <= max(calc_type) / max_input.
    // With f = n/d * 2^E and t = E + bitShifts: floor(n * 2^t / d). For t < 0 the division by 2^-t
    // comes after the one by d: floor(floor(n/d) / 2^-t) is the same value, and for the nearest one
    // the fraction r/d < 1 cannot carry across the half (round half up of q/2^-t with q = floor(n/d)).
    static constexpr math_bits_detail::u128 calc_mult_floor()
    {
        const int t = f_value.exp + bitShifts;
        const math_bits_detail::u128 m = f_value.num;
        if constexpr (f_value.den == 1) return t >= 0 ? (m << t) : (m >> -t);
        else return t >= 0 ? math_bits_detail::divmod(m << t, f_value.den).quot
                           : (math_bits_detail::divmod(m, f_value.den).quot >> -t);
    }
    static constexpr math_bits_detail::u128 calc_mult_nearest()
    {
        using math_bits_detail::u128;
        const int t = f_value.exp + bitShifts;
        const u128 m = f_value.num;
        if constexpr (f_value.den == 1) return t >= 0 ? (m << t) : ((m + (u128(1) << (-t - 1))) >> -t);
        else if (t >= 0)
        {
            const math_bits_detail::u128_divmod qr = math_bits_detail::divmod(m << t, f_value.den);
            return qr.rem >= f_value.den - qr.rem ? qr.quot + u128(1) : qr.quot;   // 2r >= d
        }
        else return (math_bits_detail::divmod(m, f_value.den).quot + (u128(1) << (-t - 1))) >> -t;
    }

    // Template constants for internal calculations
    static constexpr out_type max_deviation{max_error};
    // Floating-point multiplier (for a ratio an approximation, informational only).
    static constexpr float_type mult_factor = []
    {
        if constexpr (is_ratio) return static_cast<long double>(f_value.num) / static_cast<long double>(f_value.den);
        else return multvalue;
    }();
    static constexpr io_type max_input_int{max_input_value};   // Maximum allowed input
    static constexpr uint8_t bitShifts{calc_bitshifts()};     // Number of bits to shift

    // Defense-in-depth: shift count must be < calc_type width for well-defined shift behavior
    static_assert(bitShifts < std::numeric_limits<calc_type>::digits,
                  "bitShifts must be < digits(calc_type) — required for well-defined shift behavior!");

    // Half-LSB rounding bias used by mult() when trade_speed_for_precision is enabled.
    // Zero when bitShifts == 0 (no shift, no rounding needed) — guards against UB on `1 << -1`.
    // Referenced by both mult() and the calc_type overflow assert below.
    static constexpr calc_type round_bias = (bitShifts == 0)
        ? static_cast<calc_type>(0)
        : (static_cast<calc_type>(1) << (bitShifts - 1));

    // Whether the multiply with multiplier M actually needs the rounding bias add. The half-LSB
    // bias changes a result only where the low bitShifts bits of x * M reach half. With
    // r = M mod 2^bitShifts, those bits grow by r < 2^(bitShifts-1) per input step, so they cannot
    // skip the upper half: they never reach it for any x <= max_input exactly when
    // max_input * r < 2^(bitShifts-1) (e.g. integer factors, r = 0). The results are then provably
    // identical with and without the bias, and the add is dropped (one instruction less).
    static constexpr bool bias_needed_for(calc_type M)
    {
        if (!trade_speed_for_precision || bitShifts == 0) return false;
        const uint64_t r = static_cast<uint64_t>(M) & ((uint64_t{1} << bitShifts) - 1);
        return !(math_bits_detail::mul64(static_cast<uint64_t>(max_input_int), r)
                 < (math_bits_detail::u128(1) << (bitShifts - 1)));
    }

    // Whether max_input * M + round_bias fits calc_type, i.e. the bias can be added before the
    // shift. When it does not (the product fills calc_type's top bits), mult() rounds with
    //   (((x * M) >> (bitShifts-1)) + 1) >> 1  ==  (x * M + 2^(bitShifts-1)) >> bitShifts
    // instead: with x * M = q * 2^(bitShifts-1) + r, r < 2^(bitShifts-1), both are floor((q+1)/2).
    // Same results, no room needed (one extra shift); the bit width is kept, so no precision is lost.
    // On Cortex-M0/M0+/M23 it is also used when the bias is large (see round_by_shift_for).
    static constexpr bool bias_fits_for(calc_type M)
    {
        return math_bits_detail::mul64(static_cast<uint64_t>(max_input_int), static_cast<uint64_t>(M))
               + math_bits_detail::u128(static_cast<uint64_t>(round_bias))
               <= math_bits_detail::u128(static_cast<uint64_t>(std::numeric_limits<calc_type>::max()));
    }

    // Split multiply is used for a 64-bit calc_type with out_type <= 32 bits when
    // MATH_BITS_SPLIT_MUL64 is set (auto-detected for ARMv6-M / ARMv8-M Baseline).
    // io_type may be up to 64 bits.
    static constexpr bool use_split_mul64 = (MATH_BITS_SPLIT_MUL64 != 0)
        && std::numeric_limits<calc_type>::digits == 64
        && std::numeric_limits<out_type>::digits <= 32
        && std::numeric_limits<io_type>::digits <= 64;

    // Whether mult() with multiplier M rounds by shifting (see bias_fits_for) instead of adding
    // the bias: always when the bias does not fit, and also when the constant actually added is at
    // least MATH_BITS_SHIFT_ROUND_MIN_BIAS (shorter code there). In the split multiply that constant
    // is 2^31 in the low word for bitShifts == 32, else 2^(bitShifts-33) in the high word.
    static constexpr bool round_by_shift_for(calc_type M)
    {
        constexpr uint64_t min_bias = MATH_BITS_SHIFT_ROUND_MIN_BIAS;
        if (!bias_needed_for(M)) return false;
        if (!bias_fits_for(M))   return true;
        if (min_bias == 0)       return false;
        if (use_split_mul64)
            return (bitShifts == 32 ? (uint64_t{1} << 31) : (uint64_t{1} << (bitShifts - 33))) >= min_bias;
        return static_cast<uint64_t>(round_bias) >= min_bias;
    }

    // (x * M [+ round_bias]) >> bitShifts for a 64-bit calc_type, using 32-bit
    // operations only. With M = M_hi*2^32 + M_lo and x = x_hi*2^32 + x_lo:
    //   x*M = x_lo*M_lo + (x_lo*M_hi + x_hi*M_lo)*2^32   (mod 2^64)
    // computed modulo 2^64 exactly like the plain uint64_t expression (so bit-identical, also
    // for inputs above max_input_value):
    //   - x_lo*M_lo needs all 64 bits -> 16x16 partial products (2 when io_type <= 16 bits, else 4)
    //   - the cross terms only contribute their low 32 bits -> one 32-bit multiply each;
    //     x_hi*M_lo exists only for a 64-bit io_type
    // M is a compile-time constant, so zero parts fold away (0.75 -> a single muls;
    // M_hi is always 0 when max_input_value >= 2^32).
    template<calc_type M>
    MATH_BITS_ALWAYS_INLINE static constexpr out_type mult_split64(io_type input_val)
    {
        constexpr uint32_t m_lo = static_cast<uint32_t>(M);
        constexpr uint32_t m_hi = static_cast<uint32_t>(static_cast<uint64_t>(M) >> 32);
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

        if constexpr (round_by_shift_for(M))
        {
            // Rounding without the bias add (see round_by_shift_for): (A >> s) + bit (s-1) of A.
            if constexpr (bitShifts == 32)      return static_cast<out_type>(hi + (lo >> 31));
            else if constexpr (bitShifts == 33) return static_cast<out_type>((hi >> 1) + (hi & 1u));
            else                                return static_cast<out_type>(((hi >> (bitShifts - 33)) + 1u) >> 1);
        }
        else if constexpr (bias_needed_for(M))
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

    // Type of the multiply in mult_core: calc_type, widened to at least unsigned int. A narrower
    // calc_type (uint8_t, uint16_t) would be promoted to signed int, where an input above
    // max_input_value could overflow (undefined behaviour). Unsigned arithmetic wraps, and the
    // product is cast back to calc_type, so the result is the same as a calc_type-wide multiply.
    using calc_mul_type = std::common_type_t<calc_type, unsigned int>;

    // The multiply itself for multiplier M (no clamp): (x * M [+ round_bias]) >> bitShifts.
    // A template so the self-test can evaluate candidate multipliers with exactly this code.
    template<calc_type M>
    MATH_BITS_ALWAYS_INLINE static constexpr out_type mult_core(io_type input_val)
    {
        if constexpr (use_split_mul64)
        {
            return mult_split64<M>(input_val);
        }
        else
        {
            // Scale the input using integer multiplier
            calc_type output_val = static_cast<calc_type>(static_cast<calc_mul_type>(input_val) * M);
            if constexpr (round_by_shift_for(M))
            {
                // Round half up without the bias add (see round_by_shift_for). For bitShifts >= 2 the
                // + 1 cannot overflow; for bitShifts == 1 use (A >> 1) + (A & 1) instead.
                if constexpr (bitShifts >= 2)
                    return static_cast<out_type>(static_cast<calc_type>((output_val >> (bitShifts - 1)) + 1u) >> 1);
                else
                    return static_cast<out_type>((output_val >> 1) + (output_val & 1u));
            }
            else if constexpr (bias_needed_for(M))
            {
                // Half-LSB bias so the shift below produces round-half-up output —
                // mult() then exactly matches (out_type)round(input * mult_factor) when
                // bitShifts headroom permits. Skipped when it provably changes no result.
                output_val += round_bias;
            }
            output_val = output_val >> bitShifts; // Divide by 2^bitShifts
            return static_cast<out_type>(output_val); // Cast to the output type
        }
    }

    // ---- Multiplier selection -------------------------------------------------------------
    //
    // multvalue * 2^bitShifts is generally not an integer, so the multiplier is rounded: by default
    // to nearest. The other direction (down instead of up, or vice versa) is sometimes more
    // accurate — e.g. it can make every result equal to the floating-point expression where the
    // nearest multiplier is 1 LSB off at exact multiples. Only the constant differs (no runtime
    // cost), so the other direction is chosen when it is verifiably better against the same
    // target (the self-test reference), never worse:
    //   (a) its verified worst-case error E_alt is lower than the nearest multiplier's E_nearest, and
    //   (b) its proven bound ceil(max_input * |e_alt| + rho) is <= E_nearest, so even a miss in the
    //       (possibly sampled) verification cannot make it worse than the nearest multiplier.
    // On a tie the nearest multiplier is kept. Verification = endpoint + targeted check of the
    // self-test at that error level (the full self-test still runs on the chosen multiplier).

    // A candidate multiplier M at error level E, shaped like this class for the self-test.
    template<calc_type M, uint64_t E>
    struct probe
    {
        using mult_type  = probe;
        using float_type = typename mult_bitshift::float_type;
        using io_type    = typename mult_bitshift::io_type;
        using out_type   = typename mult_bitshift::out_type;
        using calc_type  = typename mult_bitshift::calc_type;
        static constexpr bool       is_ratio        = mult_bitshift::is_ratio;
        static constexpr bool       trade_speed_for_precision = mult_bitshift::trade_speed_for_precision;
        static constexpr out_type   max_deviation   = static_cast<out_type>(E);
        static constexpr io_type    max_input_int   = mult_bitshift::max_input_int;
        static constexpr float_type mult_factor     = mult_bitshift::mult_factor;
        static constexpr math_bits_detail::rational f_value = mult_bitshift::f_value;
        static constexpr uint8_t    bitShifts       = mult_bitshift::bitShifts;
        static constexpr calc_type  mult_factor_int = M;
        static constexpr out_type mult(io_type x) { return mult_bitshift::template mult_core<M>(x); }
    };

    template<calc_type M, uint64_t E>
    static constexpr bool candidate_passes()
    {
        using U = unit_test_mult_bitshift<probe<M, E>, deep_test>;
        return U::endpoint_passed && U::gray_zone_passed;
    }

    // Candidate multipliers, as exact 128-bit values. The alternative is the other rounding
    // direction: one below when nearest rounded up, else one above. One above also covers an
    // exact multvalue * 2^bitShifts (nearest == floor): the floating-point expression rounds each
    // product to nearest, which can lift a product lying just below an integer onto it
    // (e.g. 3000 * (1.0/3) -> 1000), and only a slightly larger multiplier reproduces that.
    // (Rounding can never push a product below an integer, so one step below is never useful.)
    static constexpr math_bits_detail::u128 mult_nearest_u128 = calc_mult_nearest();
    static constexpr math_bits_detail::u128 mult_alt_u128 =
        (mult_nearest_u128 == calc_mult_floor()) ? mult_nearest_u128 + math_bits_detail::u128(1)
                                                 : calc_mult_floor();

    // True when the candidate is non-zero and max_input_value * candidate (+ bias) fits calc_type.
    // The bias room is still required for the alternative, although mult() could round without it
    // (bias_fits_for): dropping it would let some configurations switch multipliers.
    static constexpr bool candidate_fits(const math_bits_detail::u128& m)
    {
        using math_bits_detail::u128;
        constexpr u128 calc_max = static_cast<uint64_t>(std::numeric_limits<calc_type>::max());
        return m.hi == 0 && m.lo != 0 && m <= calc_max
            && math_bits_detail::mul64(static_cast<uint64_t>(max_input_int), m.lo)
               + u128(trade_speed_for_precision ? static_cast<uint64_t>(round_bias) : uint64_t{0}) <= calc_max;
    }

    // The default (round-to-nearest) multiplier, exposed for inspection.
    static constexpr calc_type mult_factor_int_nearest = static_cast<calc_type>(mult_nearest_u128.lo);

    static constexpr calc_type choose_mult_factor_int()
    {
        constexpr calc_type nearest = mult_factor_int_nearest;
        if constexpr (mult_alt_u128 == mult_nearest_u128 || !candidate_fits(mult_alt_u128))
        {
            return nearest;                                    // exact multiplier, or no valid alternative
        }
        else
        {
            constexpr calc_type alt = static_cast<calc_type>(mult_alt_u128.lo);
            using UN = unit_test_mult_bitshift<probe<nearest, 0>, deep_test>;
            using UA = unit_test_mult_bitshift<probe<alt, 0>, deep_test>;
            constexpr uint64_t n_floor = UN::worst_deviation_floor(), a_floor = UA::worst_deviation_floor();
            constexpr bool     n_int   = UN::worst_deviation_is_integer(), a_int = UA::worst_deviation_is_integer();
            constexpr uint64_t n_ceil  = n_floor + (n_int ? 0 : 1), a_ceil = a_floor + (a_int ? 0 : 1);

            // (a) and (b) can only both hold when the two share the proven bound, the alternative's
            // lower level floor(bound_alt) lies below it, and the nearest one's level is that bound.
            if constexpr (!UN::analysis_applies || n_ceil == 0 || a_ceil != n_ceil || a_int
                          || n_ceil > static_cast<uint64_t>(std::numeric_limits<out_type>::max()))
            {
                return nearest;
            }
            else if constexpr (!n_int && candidate_passes<nearest, n_floor>())
            {
                return nearest;                                // nearest already reaches the lower level
            }
            else if constexpr (candidate_passes<alt, a_floor>())
            {
                return alt;                                    // verified lower error, bound not worse
            }
            else
            {
                return nearest;
            }
        }
    }

    static constexpr calc_type mult_factor_int{choose_mult_factor_int()}; // Integer multiplier

    // Defense-in-depth: max_input_value * mult_factor_int must not overflow calc_type at runtime.
    // (The rounding bias needs no room: without it, mult() rounds by shifting, see bias_fits_for.)
    static_assert(math_bits_detail::mul64(static_cast<uint64_t>(max_input_int), static_cast<uint64_t>(mult_factor_int))
                  <= math_bits_detail::u128(static_cast<uint64_t>(std::numeric_limits<calc_type>::max())),
                  "max_input_value * mult_factor_int would overflow calc_type — choose a wider calc_type or smaller max_input_value!");

    // Whether mult() applies the rounding bias (see bias_needed_for), and whether it is added
    // before the shift (true) or done by the shift-based rounding (false, see round_by_shift_for).
    static constexpr bool rounding_bias_needed = bias_needed_for(mult_factor_int);
    static constexpr bool rounding_bias_added  = rounding_bias_needed && !round_by_shift_for(mult_factor_int);

    // Precomputed maximum output: mult(max_input_int). Used by the clamp_input early-return path,
    // and exposed publicly so callers can query the maximum value mult() will ever return.
    // Computed by the multiply itself, so the clamp boundary is monotonic by construction
    // (mult(max_input_int+1) cannot step down from mult(max_input_int)).
    static constexpr out_type max_output_int = mult_core<mult_factor_int>(max_input_int);

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
        return mult_core<mult_factor_int>(input_val);
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

// Exact ratio form: mult_bitshift_ratio<P, Q, max_input_value, ...> scales by P/Q (see
// math_bits_ratio; the class reduces P/Q itself).
template<uint64_t P, uint64_t Q, auto max_input_value,
         typename IoType=uint32_t, typename CalcType=uint32_t,
         typename Options = mult_bitshift_options>
using mult_bitshift_ratio = mult_bitshift<math_bits_ratio{ P, Q }, max_input_value, IoType, CalcType, Options>;

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
