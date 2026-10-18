# Decodes mbb::r[] of math_bits_bench.inc: one file per build, each holding a hex dump of r (320 bytes).
# Usage: python bench_decode.py bench_Os.hex [bench_O0.hex ...]
import sys, struct
names1 = ['base', 'sA1', 'sA0', 'dA1', 'dA0', 'fA0', 'cA_const', 'cA_rt', 'baseT', 'sT0', 'dT', 'dTn', 'cT_const', 'cT_rt']
names2 = ['base2', 'setA1', 'setA0', 'setF1', 'setF0', 'base2T', 'setT', 'setTn', 'prepT', 'applyT']
for f in sys.argv[1:]:
    w = struct.unpack('<80I', bytes.fromhex(open(f).read().strip().replace(' ', '')))
    print('==', f, 'magic %08x done %d clk %d ACR %08x mism %d' % w[:5])
    res = {}
    for k, n in enumerate(names1 + names2):
        mn, sm, mx = w[8 + 3 * k: 11 + 3 * k]; res[n] = (mn, sm / 256, mx)
    for grp, base in ((names1[:8], 'base'), (names1[8:], 'baseT'), (names2[:5], 'base2'), (names2[5:], 'base2T')):
        b = res[base][0]
        for n in grp:
            mn, av, mx = res[n]
            print('  %-9s raw %4d/%7.1f/%4d   net min %4d avg %7.1f max %4d' % (n, mn, av, mx, mn - b, av - b, mx - b))
