# Model of mult_bitshift_ratio with rounding = ceiling (used by ceil_verdict_test.py): shift, multiplier and final step as in math_bits.h,
# and the exact worst error over all x <= max_in (per residue class of x mod Q the error is monotone in x).
from math import gcd


def lib_shift(P, Q, xmax, N):
    s = N - 1
    while s > 0 and P * xmax * (1 << s) > ((1 << N) - 1) * Q:   # f * max_in * 2^s <= calc_max
        s -= 1
    return s


def model(P, Q, xmax, N):
    g = gcd(P, Q); P //= g; Q //= g
    s = lib_shift(P, Q, xmax, N)
    M = (P << s) // Q
    exact = (M * Q == P << s)
    def mult(x):
        if exact:
            return (x * M + (1 << s) - 1) >> s
        return ((x * M) >> s) + (1 if x else 0)
    return P, Q, s, M, exact, mult


def worst(P, Q, xmax, N):
    P, Q, s, M, exact, mult = model(P, Q, xmax, N)
    lo = hi = 0
    for x0 in range(min(Q, xmax + 1)):
        for x in {x0 + (xmax - x0) // Q * Q, x0}:
            q, r = divmod(x * P, Q)
            e = mult(x) - (q + (r != 0))
            lo = min(lo, e); hi = max(hi, e)
    return dict(P=P, Q=Q, s=s, exact=exact, lo=lo, hi=hi)


if __name__ == '__main__':
    cases = [("1/48 32-bit u64", 1, 48, 0xFFFFFFFF, 64), ("3300/4095 u32", 3300, 4095, 4095, 32),
             ("3300/4095 u64", 3300, 4095, 4095, 64), ("7/10 1e6 u64", 7, 10, 10**6, 64),
             ("1/1000 24-bit u64", 1, 1000, (1 << 24) - 1, 64), ("1/1000 16-bit u32", 1, 1000, 65535, 32),
             ("1/3 16-bit u32", 1, 3, 65535, 32), ("1/48000 40-bit u64", 1, 48000, (1 << 40) - 1, 64),
             ("1/1000 32-bit io64", 1, 1000, 0xFFFFFFFF, 64), ("3/4 16-bit u32", 3, 4, 65535, 32),
             ("5/16 32-bit u64", 5, 16, 0xFFFFFFFF, 64), ("1/2 4095 u32", 1, 2, 4095, 32),
             ("1/2 32-bit u64", 1, 2, 0xFFFFFFFF, 64), ("1/999983 40-bit u64", 1, 999983, (1 << 40) - 1, 64),
             ("1/7 4e9 u64", 1, 7, 4000000000, 64), ("2/3 4095 u16", 2, 3, 4095, 16)]
    for n, P, Q, xm, N in cases:
        print(n.ljust(22), worst(P, Q, xm, N))
