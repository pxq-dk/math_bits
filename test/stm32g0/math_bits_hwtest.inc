// math_bits hardware test for STM32G0 (see README.md in this folder). Include it in one .cpp of a
// test build and call mbt::run() after the HAL and clock init; it never returns.
// Results in mbt::r[] (read over SWD): see the index enum. No BKPT: failures are counted and
// the first one of each kind is recorded, so a whole run can be evaluated afterwards.
#include "math_bits.h"
#include "stm32g0xx.h"

namespace mbt
{
enum : uint32_t
{
    MAGIC = 0, PHASE, DONE, MS,
    FCHK, FFAIL, SFAIL, FF_ID, FF_X, FF_OUT, FF_REF, FF_EXTRA, SCHK, SF_ID, SF_GOT, SF_EXP,
    SBASE = 16                   // stress scenario k: SBASE + 8*k + {CHECKS, ERRORS, ISR, WRITES, FX, FOUT, FA, FB}
};
enum : uint32_t { CHECKS = 0, ERRORS, ISR, WRITES, FX, FOUT, FA, FB };
constexpr uint32_t NSCEN = 8;
volatile uint32_t r[SBASE + 8 * NSCEN];

uint32_t rng_main = 0x12345678u;
uint32_t rng_isr  = 0x9E3779B9u;
inline uint32_t xs32(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }

void ffail(uint32_t id, uint64_t x, uint64_t out, uint64_t ref, uint32_t extra)
{
    if (r[FFAIL]++ == 0) { r[FF_ID] = id; r[FF_X] = (uint32_t)x; r[FF_OUT] = (uint32_t)out; r[FF_REF] = (uint32_t)ref; r[FF_EXTRA] = extra; }
}

// ---------------- Phase 1: static scalers on the target ----------------
template<class T> uint64_t pick_x(int i)
{
    const uint64_t mi = (uint64_t)T::max_input_int;
    if (i < 600) return (uint64_t)i <= mi ? (uint64_t)i : mi;
    if (i < 1200) return mi - (uint64_t)(i - 600) <= mi ? mi - (uint64_t)(i - 600) : 0;
    return (((uint64_t)xs32(rng_main) << 32) | xs32(rng_main)) % (mi + 1);
}

// Floating-point multvalue: reference = the target's own soft-float double expression.
template<class T> void check_double(uint32_t id, double f)
{
    for (int i = 0; i < 4000; ++i)
    {
        const uint64_t x = pick_x<T>(i);
        double v = (double)x * f;
        if (T::trade_speed_for_precision) v += 0.5;
        const uint64_t ref = (uint64_t)v;
        const uint64_t out = (uint64_t)T::mult((typename T::io_type)x);
        ++r[FCHK];
        const uint64_t d = out > ref ? out - ref : ref - out;
        if (d > (uint64_t)T::max_error) ffail(id, x, out, ref, 0);
    }
}

// Ratio: reference = exact integer math (x < 2^32, P < 2^32: x*P fits 64 bits).
template<class T> void check_ratio(uint32_t id)
{
    const uint64_t P = T::f_value.num, Q = T::f_value.den;
    for (int i = 0; i < 4000; ++i)
    {
        const uint64_t x = pick_x<T>(i);
        uint64_t ref = (x * P) / Q;
        if (T::trade_speed_for_precision && 2 * ((x * P) % Q) >= Q) ++ref;
        const uint64_t out = (uint64_t)T::mult((typename T::io_type)x);
        ++r[FCHK];
        const uint64_t d = out > ref ? out - ref : ref - out;
        if (d > (uint64_t)T::max_error) ffail(id, x, out, ref, 0);
    }
}

struct me60 : mult_bitshift_options { static constexpr uint64_t max_error = 60; };
struct rnd60 : me60 { static constexpr bool trade_speed_for_precision = true; };
struct e0 : mult_bitshift_options { static constexpr uint64_t max_error = 0; };
struct e0r : e0 { static constexpr bool trade_speed_for_precision = true; };
struct e0o : e0 { using out_type = uint32_t; };

void phase_static()
{
    check_double<mult_bitshift<3300.0 / 4095, 4095u, uint16_t, uint32_t>>(1, 3300.0 / 4095);
    check_double<mult_bitshift<1.0 / 7, 4000000000u, uint32_t, uint64_t>>(2, 1.0 / 7);
    check_double<mult_bitshift<0.0621, 1160000u, uint32_t, uint32_t, me60>>(3, 0.0621);
    check_double<mult_bitshift<0.7, 1000u, uint16_t, uint16_t, rnd60>>(4, 0.7);
    check_double<mult_bitshift<1.0 / 48000, 0xFFFFFFFFu, uint32_t, uint64_t>>(5, 1.0 / 48000);
    check_ratio<mult_bitshift_ratio<1000, 48000, 0xFFFFFFFFu, uint32_t, uint64_t, e0>>(10);
    check_ratio<mult_bitshift_ratio<3300, 4095, 4095u, uint16_t, uint32_t, e0>>(11);
    check_ratio<mult_bitshift_ratio<1, 3, 16777215u, uint32_t, uint64_t, e0r>>(12);
    check_ratio<mult_bitshift_ratio<7, 10, 1000000u, uint32_t, uint64_t, e0>>(13);
    check_ratio<mult_bitshift_ratio<1, 1000, 0xFFFFFFFFull, uint64_t, uint64_t, e0o>>(14);
}

// ---------------- Dynamic scalers ----------------
struct adc32 : mult_bitshift_ratio_dyn_options { static constexpr uint64_t num_min = 3000, num_max = 3600, den_min = 4000, den_max = 4095; static constexpr uint64_t max_error = 1; using topology = math_bits_topology::SPSC<>; };
struct adc64 : adc32 { static constexpr uint64_t max_error = 0; };
struct adcr  : adc64 { static constexpr bool trade_speed_for_precision = true; };
struct tick  : mult_bitshift_ratio_dyn_options { static constexpr uint64_t num_min = 1, num_max = 10, den_min = 1000, den_max = 1000000; static constexpr uint64_t max_error = 0; using out_type = uint32_t; using topology = math_bits_topology::SPSC<>; };
struct tickm : tick { using topology = math_bits_topology::MPSC<>; };
struct clampc : mult_bitshift_ratio_dyn_options { static constexpr uint64_t num_min = 1, num_max = 5000, den_min = 1, den_max = 4095; static constexpr math_bits_ratio gain_min{ 3000, 4095 }, gain_max{ 3600, 4095 }; static constexpr bool clamp_gain = true; static constexpr uint64_t max_error = 1; };
using A32 = mult_bitshift_ratio_dyn<4095u, uint16_t, uint32_t, adc32>;
using A64 = mult_bitshift_ratio_dyn<4095u, uint16_t, uint64_t, adc64>;
using AR  = mult_bitshift_ratio_dyn<4095u, uint16_t, uint64_t, adcr>;
using TK  = mult_bitshift_ratio_dyn<16777215u, uint32_t, uint64_t, tick>;
using TKM = mult_bitshift_ratio_dyn<16777215u, uint32_t, uint64_t, tickm>;
using CL  = mult_bitshift_ratio_dyn<4095u, uint16_t, uint32_t, clampc>;
constinit A32 a32{ 3300, 4095 };
constinit A64 a64{ 3300, 4095 };
constinit AR  ar { 3300, 4095 };
constinit TK  tk { 1, 48000 };
constinit TKM tkm{ 1, 48000 };
constinit CL  cl { 3300, 4095 };

template<class T> void check_dyn(uint32_t id, T& obj)
{
    using O = typename T::options;
    for (int g = 0; g < 300; ++g)
    {
        // gain: mostly in contract, some outside each limit
        const uint32_t sel = xs32(rng_main) % 10;
        uint64_t num = O::num_min + xs32(rng_main) % (O::num_max - O::num_min + 1);
        uint64_t den = O::den_min + xs32(rng_main) % (O::den_max - O::den_min + 1);
        if (sel == 0) num = O::num_max + 1 + xs32(rng_main) % 100;
        if (sel == 1 && O::den_min > 1) den = O::den_min - 1;
        if (sel == 2) den = O::den_max + 1 + xs32(rng_main) % 100;
        const auto before = obj.active_multiplier();
        typename T::status st;
        if (g & 1) st = obj.set(num, den);
        else { st = obj.prepare(num, den); if (st != T::status::rejected) obj.apply_new(); }
        // expected status and effective gain
        const bool in = num >= O::num_min && num <= O::num_max && den >= O::den_min && den <= O::den_max;
        uint64_t en = num, ed = den;
        auto exp = T::status::ok;
        if (!in) exp = T::status::rejected;
        else if (T::gain_min_restricts && num * O::gain_min.den < O::gain_min.num * den) { if (O::clamp_gain) { exp = T::status::clamped; en = O::gain_min.num; ed = O::gain_min.den; } else exp = T::status::rejected; }
        else if (T::gain_max_restricts && num * O::gain_max.den > O::gain_max.num * den) { if (O::clamp_gain) { exp = T::status::clamped; en = O::gain_max.num; ed = O::gain_max.den; } else exp = T::status::rejected; }
        ++r[SCHK];
        if (st != exp || (exp == T::status::rejected && obj.active_multiplier() != before))
            if (r[SFAIL]++ == 0) { r[SF_ID] = id; r[SF_GOT] = (uint32_t)st; r[SF_EXP] = (uint32_t)exp; }
        if (exp == T::status::rejected) continue;
        for (int i = 0; i < 60; ++i)
        {
            const uint64_t mi = (uint64_t)T::max_input_int;
            const uint64_t x = i < 10 ? (uint64_t)i : (i < 20 ? mi - (uint64_t)(i - 10) : xs32(rng_main) % (mi + 1));
            uint64_t ref = (x * en) / ed;
            if (T::trade_speed_for_precision && 2 * ((x * en) % ed) >= ed) ++ref;
            const uint64_t out = (uint64_t)obj.mult((typename T::io_type)x);
            ++r[FCHK];
            if (out < ref || out - ref > (uint64_t)O::max_error) ffail(id, x, out, ref, (uint32_t)(en << 16 | (ed & 0xFFFF)));
        }
    }
}

void phase_dyn()
{
    check_dyn(20, a32);
    check_dyn(21, a64);
    check_dyn(22, ar);
    check_dyn(23, tk);
    check_dyn(24, tkm);
    check_dyn(25, cl);
}

// ---------------- Phase 3: concurrency stress ----------------
// Scenario s: 0/1 naive 64-bit variable (negative control: writer ISR / reader ISR),
// 2/3 TK (64-bit multiplier, double buffer) writer ISR / reader ISR, 4/5 A64 (32-bit multiplier)
// writer ISR / reader ISR, 6 TKM MPSC: writers main + TIM6 ISR, reader TIM7 ISR,
// 7 TK writer main with prepare/apply_new, reader ISR.
volatile uint32_t scen = 0xFF;
constexpr int NX = 32;
uint32_t xs[NX];
uint32_t ref_a[NX], ref_b[NX];            // expected outputs for gain A / gain B
uint64_t gain_a_num, gain_a_den, gain_b_num, gain_b_den;
volatile uint32_t naive[2];               // a 64-bit value written / read as two 32-bit words
constexpr uint64_t VA = 0x1111111100000000ull, VB = 0x00000000EEEEEEEEull;   // torn = 0x11111111EEEEEEEE or 0
uint32_t isr_toggle = 0;

template<class T> T& obj_of();
template<> TK&  obj_of<TK>()  { return tk; }
template<> A64& obj_of<A64>() { return a64; }
template<> TKM& obj_of<TKM>() { return tkm; }

void serr(uint32_t s, uint32_t x, uint32_t out, uint32_t a, uint32_t b)
{
    volatile uint32_t* p = &r[SBASE + 8 * s];
    if (p[ERRORS]++ == 0) { p[FX] = x; p[FOUT] = out; p[FA] = a; p[FB] = b; }
}

template<class T> inline void reader_pass(uint32_t s, T& o, int i)
{
    const uint32_t out = (uint32_t)o.mult((typename T::io_type)xs[i]);
    ++r[SBASE + 8 * s + CHECKS];
    if (out != ref_a[i] && out != ref_b[i]) serr(s, xs[i], out, ref_a[i], ref_b[i]);
}
template<class T> inline void writer_step(T& o, uint32_t& toggle)
{
    toggle ^= 1u;
    if (toggle) o.set(gain_a_num, gain_a_den); else o.set(gain_b_num, gain_b_den);
}

inline void naive_write(uint32_t& toggle)
{
    toggle ^= 1u;
    const uint64_t v = toggle ? VA : VB;
    naive[0] = (uint32_t)v;               // two separate stores: not atomic
    naive[1] = (uint32_t)(v >> 32);
}
inline void naive_read(uint32_t s)
{
    const uint32_t lo = naive[0];
    const uint32_t hi = naive[1];         // two separate loads: not atomic
    const uint64_t v = ((uint64_t)hi << 32) | lo;
    ++r[SBASE + 8 * s + CHECKS];
    if (v != VA && v != VB) serr(s, 0, lo, hi, 0);
}

extern "C" void TIM6_DAC_LPTIM1_IRQHandler(void)        // writer ISR
{
    if (!(TIM6->SR & TIM_SR_UIF)) return;
    TIM6->SR = ~TIM_SR_UIF;
    TIM6->ARR = 2500u + xs32(rng_isr) % 3001u;          // jitter: hit every phase of the main loop
    const uint32_t s = scen;
    if (s < NSCEN) ++r[SBASE + 8 * s + ISR];
    switch (s)
    {
        case 0: naive_write(isr_toggle); break;
        case 2: writer_step(tk, isr_toggle); break;
        case 4: writer_step(a64, isr_toggle); break;
        case 6: writer_step(tkm, isr_toggle); ++r[SBASE + 8 * 6 + WRITES]; break;
        default: break;
    }
}

uint32_t reader_i = 0;
extern "C" void TIM7_LPTIM2_IRQHandler(void)            // reader ISR (higher priority)
{
    if (!(TIM7->SR & TIM_SR_UIF)) return;
    TIM7->SR = ~TIM_SR_UIF;
    TIM7->ARR = 700u + xs32(rng_isr) % 1301u;
    const uint32_t s = scen;
    if (s < NSCEN) ++r[SBASE + 8 * s + ISR];
    reader_i = (reader_i + 1) % NX;
    switch (s)
    {
        case 1: naive_read(1); break;
        case 3: reader_pass(3, tk, reader_i); break;
        case 5: reader_pass(5, a64, reader_i); break;
        case 6: reader_pass(6, tkm, reader_i); break;
        case 7: reader_pass(7, tk, reader_i); break;
        default: break;
    }
}

template<class T> void prepare_refs(T& o, uint64_t an, uint64_t ad, uint64_t bn, uint64_t bd)
{
    gain_a_num = an; gain_a_den = ad; gain_b_num = bn; gain_b_den = bd;
    const uint64_t mi = (uint64_t)T::max_input_int;
    for (int i = 0; i < NX; ++i)
    {
        xs[i] = (uint32_t)(mi / 2 + xs32(rng_main) % (mi / 2 + 1));
        ref_a[i] = (uint32_t)((xs[i] * an) / ad);       // max_error 0: exact
        ref_b[i] = (uint32_t)((xs[i] * bn) / bd);
    }
    o.set(an, ad);
}

void timers_init()
{
    RCC->APBENR1 |= RCC_APBENR1_TIM6EN | RCC_APBENR1_TIM7EN;
    (void)RCC->APBENR1;
    TIM6->CR1 = 0; TIM6->PSC = 0; TIM6->ARR = 4000; TIM6->EGR = TIM_EGR_UG; TIM6->SR = 0; TIM6->DIER = TIM_DIER_UIE;
    TIM7->CR1 = 0; TIM7->PSC = 0; TIM7->ARR = 1000; TIM7->EGR = TIM_EGR_UG; TIM7->SR = 0; TIM7->DIER = TIM_DIER_UIE;
    NVIC_SetPriority(TIM6_DAC_LPTIM1_IRQn, 2);
    NVIC_SetPriority(TIM7_LPTIM2_IRQn, 1);
    NVIC_EnableIRQ(TIM6_DAC_LPTIM1_IRQn);
    NVIC_EnableIRQ(TIM7_LPTIM2_IRQn);
}

uint32_t stress_ms = 20000;

void run_scenario(uint32_t s)
{
    r[PHASE] = 100 + s;
    isr_toggle = 0;
    uint32_t main_toggle = 0;
    const bool writer_isr = (s == 0 || s == 2 || s == 4 || s == 6);
    const bool reader_isr = (s == 1 || s == 3 || s == 5 || s == 6 || s == 7);
    scen = s;
    if (writer_isr) TIM6->CR1 = TIM_CR1_CEN;
    if (reader_isr) TIM7->CR1 = TIM_CR1_CEN;
    const uint32_t t0 = HAL_GetTick();
    while (HAL_GetTick() - t0 < stress_ms)
    {
        for (int i = 0; i < NX; ++i)
        {
            switch (s)
            {
                case 0: naive_read(0); break;
                case 1: naive_write(main_toggle); ++r[SBASE + 8 * 1 + WRITES]; break;
                case 2: reader_pass(2, tk, i); break;
                case 3: writer_step(tk, main_toggle); ++r[SBASE + 8 * 3 + WRITES]; break;
                case 4: reader_pass(4, a64, i); break;
                case 5: writer_step(a64, main_toggle); ++r[SBASE + 8 * 5 + WRITES]; break;
                case 6: writer_step(tkm, main_toggle); ++r[SBASE + 8 * 6 + WRITES]; break;
                case 7:
                    main_toggle ^= 1u;
                    if (main_toggle) tk.prepare(gain_a_num, gain_a_den); else tk.prepare(gain_b_num, gain_b_den);
                    tk.apply_new();
                    ++r[SBASE + 8 * 7 + WRITES];
                    break;
                default: break;
            }
        }
    }
    TIM6->CR1 = 0;
    TIM7->CR1 = 0;
    scen = 0xFF;
}

void run()
{
    r[MAGIC] = 0x4D425430u;                               // "MBT0"
    const uint32_t t0 = HAL_GetTick();
    r[PHASE] = 1; phase_static();
    r[PHASE] = 2; phase_dyn();
    timers_init();
    // gains: A and B differ in both words of the 64-bit TK multiplier (torn reads would show)
    prepare_refs(tk, 7, 999983, 3, 1013);     run_scenario(2); prepare_refs(tk, 7, 999983, 3, 1013); run_scenario(3);
    run_scenario(0); run_scenario(1);
    prepare_refs(a64, 3001, 4093, 3599, 4001); run_scenario(4); prepare_refs(a64, 3001, 4093, 3599, 4001); run_scenario(5);
    prepare_refs(tkm, 7, 999983, 3, 1013);    run_scenario(6);
    prepare_refs(tk, 9, 1000003 - 4, 1, 1001); run_scenario(7);
    r[MS] = HAL_GetTick() - t0;
    r[PHASE] = 3;
    r[DONE] = 1;
    for (;;) { }
}
}   // namespace mbt
