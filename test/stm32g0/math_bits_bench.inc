// math_bits benchmark: static vs runtime-gain (dyn) vs plain C division, measured per call with
// SysTick (core clock) on the target. Each case is a noinline extern "C" wrapper called through
// a function pointer; the cost of an identity wrapper (call + return + timing reads) is
// subtracted offline. Results in mbb::r[] (read over SWD).
#include "math_bits.h"
#include "stm32g0xx.h"

namespace mbb
{
#define NI __attribute__((noinline))

// ---- configurations ----
struct me1   : mult_bitshift_options { static constexpr uint64_t max_error = 1; static constexpr bool deep_test = false; };
struct exact : mult_bitshift_options { static constexpr uint64_t max_error = 0; static constexpr bool deep_test = false; };

// IN_ADC 12-bit -> mV, gain 3300/4095
using sA1  = mult_bitshift_ratio<3300, 4095, 4095u, uint16_t, uint32_t, me1>;     // static, max_error 1
using sA0  = mult_bitshift_ratio<3300, 4095, 4095u, uint16_t, uint32_t, exact>;   // static, exact
struct dadc1 : mult_bitshift_ratio_dyn_options { static constexpr uint64_t num_min = 3000, num_max = 3600, den_min = 4000, den_max = 4095;
                                                 static constexpr uint64_t max_error = 1; using topology = math_bits_topology::SPSC<>; };
struct dadc0 : dadc1 { static constexpr uint64_t max_error = 0; };
using dA1  = mult_bitshift_ratio_dyn<4095u, uint16_t, uint32_t, dadc1>;           // dyn, max_error 1 (u32 calc)
using dA0  = mult_bitshift_ratio_dyn<4095u, uint16_t, uint64_t, dadc0>;           // dyn, exact (needs u64 calc)
struct fadc1 : dadc1 { static constexpr uint64_t den_min = 4095, den_max = 4095; };
struct fadc0 : fadc1 { static constexpr uint64_t max_error = 0; };
using fA1  = mult_bitshift_ratio_dyn<4095u, uint16_t, uint32_t, fadc1>;           // dyn, fixed den 4095, max_error 1
using fA0  = mult_bitshift_ratio_dyn<4095u, uint16_t, uint64_t, fadc0>;           // dyn, fixed den 4095, exact

// 24-bit timer ticks @ 1 MHz -> ms, gain 1/1000
using sT0  = mult_bitshift_ratio<1, 1000, 16777215u, uint32_t, uint64_t, exact>;  // static, exact
struct dtk : mult_bitshift_ratio_dyn_options { static constexpr uint64_t num_min = 1, num_max = 10, den_min = 1000, den_max = 1000000;
                                               static constexpr uint64_t max_error = 0; using out_type = uint32_t; using topology = math_bits_topology::SPSC<>; };
struct dtkn : dtk { using topology = math_bits_topology::None<>; };
using dT   = mult_bitshift_ratio_dyn<16777215u, uint32_t, uint64_t, dtk>;          // dyn, 64-bit M, SPSC (seq counter)
using dTn  = mult_bitshift_ratio_dyn<16777215u, uint32_t, uint64_t, dtkn>;         // dyn, 64-bit M, no concurrency

constinit dA1 a1{ 3300, 4095 };
constinit dA0 a0{ 3300, 4095 };
constinit fA1 f1{ 3300 };
constinit fA0 f0{ 3300 };
constinit dT  tk{ 1, 1000 };
constinit dTn tkn{ 1, 1000 };
volatile uint32_t g_num_adc = 3300, g_den_adc = 4095, g_num_tk = 1, g_den_tk = 1000;

extern "C" {
// mult() cases: uint32_t f(uint32_t x)
NI uint32_t b_base(uint32_t x)     { return x; }
NI uint32_t b_sA1(uint32_t x)      { return sA1::mult((uint16_t)x); }
NI uint32_t b_sA0(uint32_t x)      { return sA0::mult((uint16_t)x); }
NI uint32_t b_dA1(uint32_t x)      { return a1.mult((uint16_t)x); }
NI uint32_t b_dA0(uint32_t x)      { return a0.mult((uint16_t)x); }
NI uint32_t b_fA0(uint32_t x)      { return f0.mult((uint16_t)x); }
NI uint32_t b_cA_const(uint32_t x) { return x * 3300u / 4095u; }                              // plain C, constant
NI uint32_t b_cA_rt(uint32_t x)    { return x * g_num_adc / g_den_adc; }                      // plain C, runtime gain
NI uint32_t b_sT0(uint32_t x)      { return sT0::mult(x); }
NI uint32_t b_dT(uint32_t x)       { return tk.mult(x); }
NI uint32_t b_dTn(uint32_t x)      { return tkn.mult(x); }
NI uint32_t b_cT_const(uint32_t x) { return x / 1000u; }                                      // plain C, constant
NI uint32_t b_cT_rt(uint32_t x)    { return (uint32_t)((uint64_t)x * g_num_tk / g_den_tk); }  // plain C, runtime gain
// set() cases: uint32_t f(uint32_t num, uint32_t den)
NI uint32_t b_base2(uint32_t n, uint32_t d)  { return n + d; }
NI uint32_t b_setA1(uint32_t n, uint32_t d)  { return (uint32_t)a1.set(n, d); }
NI uint32_t b_setA0(uint32_t n, uint32_t d)  { return (uint32_t)a0.set(n, d); }
NI uint32_t b_setF1(uint32_t n, uint32_t)    { return (uint32_t)f1.set(n); }                  // fixed den: no division
NI uint32_t b_setF0(uint32_t n, uint32_t)    { return (uint32_t)f0.set(n); }
NI uint32_t b_setT(uint32_t n, uint32_t d)   { return (uint32_t)tk.set(n, d); }
NI uint32_t b_setTn(uint32_t n, uint32_t d)  { return (uint32_t)tkn.set(n, d); }
NI uint32_t b_prepT(uint32_t n, uint32_t d)  { return (uint32_t)tk.prepare(n, d); }
NI uint32_t b_applyT(uint32_t, uint32_t)     { return (uint32_t)tk.apply_new(); }
}

using F1 = uint32_t (*)(uint32_t);
using F2 = uint32_t (*)(uint32_t, uint32_t);
enum : uint32_t { IN_ADC = 0, IN_TICK = 1 };
struct C1 { F1 f; uint32_t in; };
struct C2 { F2 f; uint32_t in; };
const C1 cases1[] = { {b_base, IN_ADC}, {b_sA1, IN_ADC}, {b_sA0, IN_ADC}, {b_dA1, IN_ADC}, {b_dA0, IN_ADC}, {b_fA0, IN_ADC}, {b_cA_const, IN_ADC}, {b_cA_rt, IN_ADC},
                      {b_base, IN_TICK}, {b_sT0, IN_TICK}, {b_dT, IN_TICK}, {b_dTn, IN_TICK}, {b_cT_const, IN_TICK}, {b_cT_rt, IN_TICK} };
const C2 cases2[] = { {b_base2, IN_ADC}, {b_setA1, IN_ADC}, {b_setA0, IN_ADC}, {b_setF1, IN_ADC}, {b_setF0, IN_ADC}, {b_base2, IN_TICK}, {b_setT, IN_TICK}, {b_setTn, IN_TICK}, {b_prepT, IN_TICK}, {b_applyT, IN_TICK} };
constexpr uint32_t N1 = sizeof(cases1) / sizeof(cases1[0]), N2 = sizeof(cases2) / sizeof(cases2[0]);

// r: MAGIC, DONE, SystemCoreClock, FLASH->ACR, mismatches, then per case {min, sum, max} (N1 + N2 cases)
constexpr uint32_t HDR = 8, NS = 256;
volatile uint32_t r[HDR + 3 * (N1 + N2)];
volatile uint32_t sink;
uint32_t in_adc[NS], in_tk[NS], num_adc[NS], den_adc[NS], num_tk[NS], den_tk[NS];

inline uint32_t xs32(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }

void run()
{
    __disable_irq();
    r[0] = 0x4D424230u;   // "MBB0"
    r[2] = SystemCoreClock;
    r[3] = FLASH->ACR;
    uint32_t s = 0x2468ACE1u;
    for (uint32_t i = 0; i < NS; ++i)
    {
        in_adc[i]  = xs32(s) % 4096u;           in_tk[i]  = xs32(s) & 0xFFFFFFu;
        num_adc[i] = 3000 + xs32(s) % 601u;     den_adc[i] = 4000 + xs32(s) % 96u;
        num_tk[i]  = 1 + xs32(s) % 10u;         den_tk[i]  = 1000 + xs32(s) % 999001u;
    }
    in_adc[0] = 4095; in_adc[1] = 0; in_tk[0] = 0xFFFFFFu; in_tk[1] = 0;

    // sanity: the exact variants must agree with plain C
    uint32_t mism = 0;
    for (uint32_t i = 0; i < NS; ++i)
    {
        mism += b_sA0(in_adc[i]) != b_cA_const(in_adc[i]);
        mism += b_dA0(in_adc[i]) != b_cA_const(in_adc[i]);
        mism += b_fA0(in_adc[i]) != b_cA_const(in_adc[i]);
        mism += b_sT0(in_tk[i]) != b_cT_const(in_tk[i]);
        mism += b_dT(in_tk[i]) != b_cT_const(in_tk[i]);
        mism += b_dTn(in_tk[i]) != b_cT_const(in_tk[i]);
    }
    r[4] = mism;

    SysTick->CTRL = 0;
    SysTick->LOAD = 0xFFFFFFu;
    SysTick->VAL  = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;   // core clock, no interrupt

    for (uint32_t k = 0; k < N1; ++k)
    {
        const uint32_t* in = cases1[k].in == IN_ADC ? in_adc : in_tk;
        uint32_t mn = ~0u, mx = 0, sum = 0;
        for (uint32_t pass = 0; pass < 2; ++pass)   // pass 0 warms the cache
            for (uint32_t i = 0; i < NS; ++i)
            {
                const uint32_t x = in[i];
                const F1 f = cases1[k].f;
                const uint32_t t0 = SysTick->VAL;
                const uint32_t y = f(x);
                const uint32_t t1 = SysTick->VAL;
                sink = y;
                const uint32_t d = (t0 - t1) & 0xFFFFFFu;
                if (pass) { mn = d < mn ? d : mn; mx = d > mx ? d : mx; sum += d; }
            }
        r[HDR + 3 * k] = mn; r[HDR + 3 * k + 1] = sum; r[HDR + 3 * k + 2] = mx;
    }
    for (uint32_t k = 0; k < N2; ++k)
    {
        const uint32_t* nn = cases2[k].in == IN_ADC ? num_adc : num_tk;
        const uint32_t* dd = cases2[k].in == IN_ADC ? den_adc : den_tk;
        uint32_t mn = ~0u, mx = 0, sum = 0;
        for (uint32_t pass = 0; pass < 2; ++pass)
            for (uint32_t i = 0; i < NS; ++i)
            {
                if (cases2[k].f == b_applyT) tk.prepare(nn[i], dd[i]);   // something to apply (outside the timing)
                const uint32_t n = nn[i], d0 = dd[i];
                const F2 f = cases2[k].f;
                const uint32_t t0 = SysTick->VAL;
                const uint32_t y = f(n, d0);
                const uint32_t t1 = SysTick->VAL;
                sink = y;
                const uint32_t d = (t0 - t1) & 0xFFFFFFu;
                if (pass) { mn = d < mn ? d : mn; mx = d > mx ? d : mx; sum += d; }
            }
        r[HDR + 3 * (N1 + k)] = mn; r[HDR + 3 * (N1 + k) + 1] = sum; r[HDR + 3 * (N1 + k) + 2] = mx;
    }
    r[1] = 1;
    for (;;) {}
}
#undef NI
} // namespace mbb
