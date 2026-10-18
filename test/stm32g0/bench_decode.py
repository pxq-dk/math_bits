# Decodes mbb::r[] of math_bits_bench.inc: one file per build, each holding a hex dump of r.
# Usage: python bench_decode.py bench_Os.hex [bench_O0.hex ...]
import sys, struct
names1 = ['base', 'sA1', 'sA0', 'dA1', 'dA0', 'fA0', 'cA_const', 'cA_rt', 'baseT', 'sT0', 'dT', 'dTn', 'cT_const', 'cT_rt',
          'baseC', 'dA1c', 'dA0c', 'fA0c', 'cA_ceil', 'baseTC', 'dTnc', 'cT_ceil',
          'baseSC', 'sA0c', 'baseSTC', 'sT0c']
names2 = ['base2', 'setA1', 'setA0', 'setF1', 'setF0', 'base2T', 'setT', 'setTn', 'prepT', 'applyT',
          'base2C', 'setA0c', 'setF0c', 'base2TC', 'setTnc']
for f in sys.argv[1:]:
    h = bytes.fromhex(open(f).read().strip().replace(' ', ''))
    w = struct.unpack('<%dI' % (len(h) // 4), h)
    print('==', f, 'magic %08x done %d clk %d ACR %08x mism %d' % w[:5])
    res = {}
    for k, n in enumerate(names1 + names2):
        mn, sm, mx = w[8 + 3 * k: 11 + 3 * k]; res[n] = (mn, sm / 256, mx)
    for grp, base in ((names1[:8], 'base'), (names1[8:14], 'baseT'), (names1[14:19], 'baseC'), (names1[19:22], 'baseTC'),
                      (names1[22:24], 'baseSC'), (names1[24:], 'baseSTC'),
                      (names2[:5], 'base2'), (names2[5:10], 'base2T'), (names2[10:13], 'base2C'), (names2[13:], 'base2TC')):
        b = res[base][0]
        for n in grp:
            mn, av, mx = res[n]
            print('  %-9s raw %4d/%7.1f/%4d   net min %4d avg %7.1f max %4d' % (n, mn, av, mx, mn - b, av - b, mx - b))
