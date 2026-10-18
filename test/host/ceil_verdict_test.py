# Compile-time verdict of mult_bitshift_ratio with rounding = ceiling against the exact worst error (from
# ceil_truth.py, a model of the library): for random ratios, max_error = W (the true worst error) must build,
# and max_error = W - 1 must fail with an error-bound message. One -fsyntax-only compile per case, 8 in parallel.
# usage (from this folder):  python -I ceil_verdict_test.py [n_random] [seed] [--pow2]
#   --pow2: power-of-two denominators (exact multipliers, the bias form)
# The compiler is $CXX (default g++); it needs C++20. Prints every disagreement; exit code 0 = none.
import os, random, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor
from math import gcd
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ceil_truth import worst

args = [a for a in sys.argv[1:] if not a.startswith('--')]
N = int(args[0]) if args else 600
SEED = int(args[1]) if len(args) > 1 else 7
POW2 = '--pow2' in sys.argv
LIB = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
GPP = os.environ.get('CXX', 'g++')
rng = random.Random(SEED)
cfgs = []
while len(cfgs) < N:
    big_q = len(cfgs) % 8 == 7                          # some large Q: the level walk instead of the residue check
    Q = (1 << rng.randint(1, 22)) if POW2 else rng.randint(100000, 1000000) if big_q else rng.randint(2, 60000)
    P = rng.randint(1, 5000)
    if Q // gcd(P, Q) == 1:
        continue
    calc = rng.choice([32, 64])
    if calc == 32:
        xmax = rng.randint(2, 1 << rng.randint(4, 24)); io = 'uint32_t'
    else:
        xmax = rng.randint(2, 1 << rng.randint(8, 44)); io = 'uint64_t'
    if P * xmax < Q or -(-P * xmax // Q) >= (1 << 32) or P * xmax >= Q * ((1 << calc) - 1):
        continue
    cfgs.append((P, Q, xmax, calc, io))

def compile_with(P, Q, xmax, calc, io, k):
    src = f'''#include "math_bits.h"
struct o : mult_bitshift_ratio_options {{ static constexpr math_bits_rounding rounding = math_bits_rounding::ceiling;
    static constexpr uint64_t max_error = {k}; using out_type = uint32_t; }};
using t = mult_bitshift_ratio<{P}u, {Q}u, {xmax}ull, {io}, uint{calc}_t, o>;
constexpr auto v = t::mult_factor_int;
'''
    fd, path = tempfile.mkstemp(suffix='.cpp'); os.write(fd, src.encode()); os.close(fd)
    r = subprocess.run([GPP, '-std=c++20', '-fsyntax-only', '-fconstexpr-ops-limit=4000000000', '-I', LIB, path],
                       capture_output=True, text=True)
    os.remove(path)
    err = [l for l in r.stderr.splitlines() if 'error:' in l]
    return r.returncode == 0, err

def run(c):
    P, Q, xmax, calc, io = c
    w = worst(P, Q, xmax, calc)
    W = -w['lo']
    ok_at_w, err_w = compile_with(P, Q, xmax, calc, io, W)
    res = dict(cfg=c, W=W, exact=w['exact'], pass_at_W=ok_at_w, err_W=err_w[:2])
    if W >= 1:
        ok_below, err_b = compile_with(P, Q, xmax, calc, io, W - 1)
        bound_msg = any(('max_error' in e or 'Targeted' in e or 'unit-testing' in e) for e in err_b)
        res.update(fail_below=not ok_below, bound_msg=bound_msg, err_below=err_b[:1])
    return res

with ThreadPoolExecutor(max_workers=8) as ex:
    results = list(ex.map(run, cfgs))
bad = 0
for r in results:
    P, Q, xmax, calc, io = r['cfg']
    flag = ''
    if not r['pass_at_W']: flag = 'REJECTS TRUE BOUND ' + str(r['err_W'])
    elif r['W'] >= 1 and not r['fail_below']: flag = 'ACCEPTS W-1' + (' (large Q, level walk)' if Q >= 100000 else '')
    elif r['W'] >= 1 and not r['bound_msg']: flag = 'fails for another reason: ' + str(r['err_below'])
    if flag:
        bad += 1
        print(f'{P}/{Q} max={xmax} calc={calc} W={r["W"]}: {flag}')
nW = sum(1 for r in results if r['W'] >= 1)
print(f'{len(results)} configs ({nW} with W >= 1, {sum(1 for r in results if r["exact"])} exact multipliers), {bad} disagreements')
sys.exit(1 if bad else 0)
