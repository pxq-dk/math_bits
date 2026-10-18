# Decodes mbt::r[] of math_bits_hwtest.inc: hex dump of r (from the debugger) on stdin.
# Usage: python hwtest_decode.py < dump.txt
import sys, struct
h = bytes.fromhex(sys.stdin.read().strip().replace(' ', ''))
w = struct.unpack('<%dI' % (len(h) // 4), h)
names = ['MAGIC', 'PHASE', 'DONE', 'MS', 'FCHK', 'FFAIL', 'SFAIL', 'FF_ID', 'FF_X', 'FF_OUT', 'FF_REF', 'FF_EXTRA', 'SCHK', 'SF_ID', 'SF_GOT', 'SF_EXP']
print('magic %08x phase %d done %d  run %.1f s' % (w[0], w[1], w[2], w[3] / 1000))
print('functional: %d output checks, %d failures | %d status checks, %d status failures' % (w[4], w[5], w[12], w[6]))
if w[5]: print('  first output failure: cfg %d x=%d out=%d ref=%d extra=%08x' % (w[7], w[8], w[9], w[10], w[11]))
if w[6]: print('  first status failure: cfg %d got %d expected %d' % (w[13], w[14], w[15]))
scen = ['naive 64-bit  writer ISR / reader main (neg. control)', 'naive 64-bit  writer main / reader ISR (neg. control)',
        'TK 64-bit M   writer ISR / reader main', 'TK 64-bit M   writer main / reader ISR',
        'A64 32-bit M  writer ISR / reader main', 'A64 32-bit M  writer main / reader ISR',
        'TKM MPSC      writers main+ISR / reader ISR', 'TK prepare/apply_new main / reader ISR']
for k in range(8):
    b = 16 + 8 * k
    if b + 8 > len(w): break
    c, e, i, wr, fx, fo, fa, fb = w[b:b + 8]
    line = '%d %-56s checks %10d  errors %8d  isr %8d  writes %9d' % (k, scen[k], c, e, i, wr)
    if e: line += '  first: x=%d out=%08x a=%08x b=%08x' % (fx, fo, fa, fb)
    print(line)
