// Host test of rounding = ceiling: mult() against the exact ceil(x*num/den), error must be in [-max_error, 0].
// mult_bitshift_ratio_dyn: many gains per configuration, exhaustive in x for 12/16-bit inputs, else edges +
// random + worst residues. mult_bitshift_ratio: the smallest and largest input of every residue class of
// x mod Q, which gives the exact worst error (it is monotone in x within a class); it must equal the expected
// value (from a model of the library), so max_error = that value is the tightest bound mult() meets.
// Prints per configuration the shift, the multiplier width and which ceiling form is used; exit code 0 = pass.
// Build and run from this folder (any C++20 host compiler with unsigned __int128, e.g. GCC or Clang), once with
// the split 32x64 multiply of Cortex-M0/M0+/M23 forced on and once off:
//     g++ -std=c++20 -O2 -DMATH_BITS_SPLIT_MUL64=1 -I../.. math_bits_ceil_test.cpp -o ceil_test && ./ceil_test
//     g++ -std=c++20 -O2 -DMATH_BITS_SPLIT_MUL64=0 -I../.. math_bits_ceil_test.cpp -o ceil_test && ./ceil_test
#include "math_bits.h"
#include <cstdio>
#include <numeric>
#include <random>
using u128 = unsigned __int128;
using R = math_bits_rounding;
static std::mt19937_64 rng(12345);
static long fails = 0, checks = 0;

template<class T> void run(const char* name, uint64_t gains)
{
    using O = typename T::options;
    const uint64_t mi = (uint64_t)T::max_input_int, io_max = (uint64_t)std::numeric_limits<typename T::io_type>::max();
    long worst = 0;
    T obj{ O::num_min + (O::num_max - O::num_min) / 2, O::den_min + (O::den_max - O::den_min) / 2 };
    for (uint64_t g = 0; g < gains; ++g)
    {
        uint64_t num = O::num_min + (g < 4 ? (g & 1) * (O::num_max - O::num_min) : rng() % (O::num_max - O::num_min + 1));
        uint64_t den = O::den_min + (g < 4 ? (g >> 1) * (O::den_max - O::den_min) : rng() % (O::den_max - O::den_min + 1));
        auto st = obj.set(num, den);
        if (st == T::status::rejected) continue;
        const auto e = T::evaluate(num, den);
        if (obj.active_multiplier() != e.m) { printf("%s: active multiplier mismatch\n", name); ++fails; }
        auto check = [&](uint64_t x)
        {
            const uint64_t xc = (O::clamp_input && x > mi) ? mi : x;
            if (xc > mi) return;                                   // outside the contract, no reference
            const u128 p = (u128)xc * e.num;
            uint64_t ref = (uint64_t)(p / e.den) + (p % e.den != 0);
            if (O::clamp_output && ref > O::output_limit) ref = O::output_limit;
            const uint64_t out = (uint64_t)obj.mult((typename T::io_type)x);
            ++checks;
            const long err = (long)out - (long)ref;
            if (err < worst) worst = err;
            if (err > 0 || -err > (long)O::max_error)
            { if (fails++ < 10) printf("%s FAIL gain %llu/%llu x=%llu out=%llu ref=%llu\n", name, (unsigned long long)e.num, (unsigned long long)e.den, (unsigned long long)x, (unsigned long long)out, (unsigned long long)ref); }
        };
        if (mi < 70000) for (uint64_t x = 0; x <= mi; ++x) check(x);
        else
        {
            for (uint64_t i = 0; i < 2000; ++i) { check(i); check(mi - i); check(rng() % (mi + 1)); }
            if (std::gcd(e.num, e.den) == 1 && e.den >= 2)            // worst residues: x*num == 1, -1 (mod den)
                for (uint64_t r0 = 1; r0 < 4 && r0 < e.den; ++r0)
                    for (uint64_t x0 = 0; x0 < e.den && x0 <= mi; ++x0)
                        if ((u128)x0 * e.num % e.den == r0 || (u128)x0 * e.num % e.den == e.den - r0)
                            for (uint64_t x = x0 + (mi - x0) / e.den * e.den, i = 0; i < 50; ++i, x -= e.den) { check(x); if (x < x0 + e.den) break; }
        }
        if (O::clamp_input) for (uint64_t i = 0; i < 64; ++i) { check(io_max - i); check(mi + 1 + rng() % (io_max - mi)); }
    }
    printf("%-10s s=%2d M%2d-bit split=%d xnz=%d sticky=%d k=%llu  worst error %ld\n", name, (int)T::bitShifts, (int)sizeof(typename T::m_type) * 8,
           (int)T::use_split_mul64, (int)T::ceil_xnz, (int)T::ceil_by_sticky, (unsigned long long)O::max_error, worst);
}

struct ceil_opts : mult_bitshift_ratio_dyn_options { static constexpr R rounding = R::ceiling; };
struct c_adc1  : ceil_opts { static constexpr uint64_t num_min = 3000, num_max = 3600, den_min = 4000, den_max = 4095, max_error = 1; };
struct c_adc0  : c_adc1  { static constexpr uint64_t max_error = 0; };
struct c_fix0  : c_adc0  { static constexpr uint64_t den_min = 4095, den_max = 4095; };
struct c_fix1  : c_adc1  { static constexpr uint64_t den_min = 4095, den_max = 4095; };
struct c_tick  : ceil_opts { static constexpr uint64_t num_min = 1, num_max = 10, den_min = 1000, den_max = 1000000, max_error = 0; using out_type = uint32_t; using topology = math_bits_topology::SPSC<>; };
struct c_tkfix : c_tick  { static constexpr uint64_t den_min = 1000, den_max = 1000; };
struct c_clamp : ceil_opts { static constexpr uint64_t num_min = 1, num_max = 5000, den_min = 1, den_max = 4095, max_error = 1, output_limit = 3300;
                             static constexpr math_bits_ratio gain_min{ 3000, 4095 }, gain_max{ 3600, 4095 }; static constexpr bool clamp_gain = true, clamp_output = true, clamp_input = true; };
struct c_stk32 : ceil_opts { static constexpr uint64_t num_min = 1, num_max = 1, den_min = 2, den_max = 3, max_error = 0; };
struct c_stk64 : c_stk32 { using out_type = uint32_t; };
struct c_q12   : ceil_opts { static constexpr uint64_t num_min = 1, num_max = 8191, den_min = 4096, den_max = 4096, max_error = 0; using out_type = uint32_t; };
struct c_zero  : c_adc1  { static constexpr uint64_t num_min = 0; };                     // gain 0 allowed: bias form
struct c_p2sp  : ceil_opts { static constexpr uint64_t num_min = 1, num_max = 1 << 20, den_min = 1 << 20, den_max = 1 << 20, max_error = 0; using out_type = uint32_t; };
struct c_big   : ceil_opts { static constexpr uint64_t num_min = 1, num_max = 1000, den_min = 1, den_max = 1000, max_error = 2; using out_type = uint32_t; };

// ---- mult_bitshift_ratio ----
template<uint64_t K> struct rc  : mult_bitshift_ratio_options { static constexpr R rounding = R::ceiling; static constexpr uint64_t max_error = K; };
template<uint64_t K> struct rco : rc<K> { using out_type = uint32_t; };

template<class T> void run_static(const char* name, long expected_worst)
{
    const uint64_t P = T::f_value.num, Q = T::f_value.den, mi = (uint64_t)T::max_input_int;
    long worst = 0;
    auto check = [&](uint64_t x)
    {
        const u128 p = (u128)x * P;
        const uint64_t ref = (uint64_t)(p / Q) + (p % Q != 0);
        const uint64_t out = (uint64_t)T::mult((typename T::io_type)x);
        ++checks;
        const long err = (long)out - (long)ref;
        if (err < worst) worst = err;
        if (err > 0 || -err > (long)T::max_error)
        { if (fails++ < 10) printf("%s FAIL x=%llu out=%llu ref=%llu\n", name, (unsigned long long)x, (unsigned long long)out, (unsigned long long)ref); }
    };
    for (uint64_t x0 = 0; x0 < Q && x0 <= mi; ++x0) { check(x0); check(x0 + (mi - x0) / Q * Q); }
    for (int i = 0; i < 20000; ++i) check(rng() % (mi + 1));
    if (worst != -expected_worst) { printf("%s: worst error %ld, expected %ld\n", name, worst, -expected_worst); ++fails; }
    using U = typename T::self_test;
    // level_start(n) (used by the level walk): the smallest x with ceil(x*P/Q) >= n, else max_input.
    const uint64_t n_max = (uint64_t)U::reference(T::max_input_int);
    for (int i = 0; i < 20000; ++i)
    {
        const uint64_t n = i < 4 ? (i < 2 ? (uint64_t)i : n_max - (i - 2)) : rng() % (n_max + 1);
        const uint64_t x = U::level_start(n);
        ++checks;
        if ((uint64_t)U::reference((typename T::io_type)x) < n || (x > 0 && (uint64_t)U::reference((typename T::io_type)(x - 1)) >= n))
        { if (fails++ < 10) printf("%s FAIL level_start(%llu) = %llu\n", name, (unsigned long long)n, (unsigned long long)x); }
    }
    printf("%-22s s=%2d xnz=%d proven_exact=%d residue_check=%d k=%llu  worst error %ld\n", name, (int)T::bitShifts, (int)T::ceil_xnz,
           (int)U::ratio_exact, (int)U::residue.applies, (unsigned long long)T::max_error, worst);
}

int main()
{
    run<mult_bitshift_ratio_dyn<4095u, uint16_t, uint32_t, c_adc1>>("adc k1 u32", 400);
    run<mult_bitshift_ratio_dyn<4095u, uint16_t, uint64_t, c_adc0>>("adc k0 u64", 400);
    run<mult_bitshift_ratio_dyn<4095u, uint16_t, uint64_t, c_fix0>>("fix0 u64", 400);
    run<mult_bitshift_ratio_dyn<4095u, uint16_t, uint32_t, c_fix1>>("fix1 u32", 400);
    run<mult_bitshift_ratio_dyn<16777215u, uint32_t, uint64_t, c_tick>>("tick", 300);
    run<mult_bitshift_ratio_dyn<16777215u, uint32_t, uint64_t, c_tkfix>>("tick fix", 50);
    run<mult_bitshift_ratio_dyn<4095u, uint16_t, uint32_t, c_clamp>>("clamp", 400);
    run<mult_bitshift_ratio_dyn<4095u, uint16_t, uint32_t, c_stk32>>("sticky32", 20);
    run<mult_bitshift_ratio_dyn<0xFFFFFFFFu, uint32_t, uint64_t, c_stk64>>("sticky64", 20);
    run<mult_bitshift_ratio_dyn<4095u, uint16_t, uint32_t, c_q12>>("q12 u32", 400);
    run<mult_bitshift_ratio_dyn<65535u, uint16_t, uint64_t, c_big>>("big", 200);
    run<mult_bitshift_ratio_dyn<4095u, uint16_t, uint32_t, c_zero>>("zero ok", 300);
    run<mult_bitshift_ratio_dyn<16777215u, uint32_t, uint64_t, c_p2sp>>("pow2 split", 100);
    run_static<mult_bitshift_ratio<1, 48, 0xFFFFFFFFu, uint32_t, uint64_t, rc<0>>>("1/48 32-bit u64", 0);
    run_static<mult_bitshift_ratio<3300, 4095, 4095u, uint16_t, uint32_t, rc<0>>>("3300/4095 u32", 0);
    run_static<mult_bitshift_ratio<3300, 4095, 4095u, uint16_t, uint64_t, rc<0>>>("3300/4095 u64", 0);
    run_static<mult_bitshift_ratio<7, 10, 1000000u, uint32_t, uint64_t, rc<0>>>("7/10 1e6 u64", 0);
    run_static<mult_bitshift_ratio<1, 1000, 16777215u, uint32_t, uint64_t, rc<0>>>("1/1000 24-bit u64", 0);
    run_static<mult_bitshift_ratio<1, 1000, 65535u, uint32_t, uint32_t, rc<0>>>("1/1000 16-bit u32", 0);
    run_static<mult_bitshift_ratio<1, 3, 65535u, uint32_t, uint32_t, rc<0>>>("1/3 16-bit u32", 0);
    run_static<mult_bitshift_ratio<1, 48000, 1099511627775ull, uint64_t, uint64_t, rco<1>>>("1/48000 40-bit u64", 1);
    run_static<mult_bitshift_ratio<1, 1000, 0xFFFFFFFFull, uint64_t, uint64_t, rco<1>>>("1/1000 32-bit io64", 1);
    run_static<mult_bitshift_ratio<3, 4, 65535u, uint32_t, uint32_t, rc<0>>>("3/4 16-bit u32", 0);
    run_static<mult_bitshift_ratio<5, 16, 0xFFFFFFFFu, uint32_t, uint64_t, rc<0>>>("5/16 32-bit u64", 0);
    run_static<mult_bitshift_ratio<1, 2, 4095u, uint16_t, uint32_t, rc<0>>>("1/2 4095 u32", 0);
    run_static<mult_bitshift_ratio<1, 2, 0xFFFFFFFFu, uint32_t, uint64_t, rc<0>>>("1/2 32-bit u64", 0);
    run_static<mult_bitshift_ratio<1, 999983, 1099511627775ull, uint64_t, uint64_t, rco<1>>>("1/999983 40-bit u64", 1);
    run_static<mult_bitshift_ratio<1, 7, 4000000000u, uint32_t, uint64_t, rc<0>>>("1/7 4e9 u64", 0);
    run_static<mult_bitshift_ratio<2, 3, 4095u, uint16_t, uint16_t, rc<171>>>("2/3 4095 u16", 171);
    printf("%ld checks, %ld failures\n", checks, fails);
    return fails != 0;
}
