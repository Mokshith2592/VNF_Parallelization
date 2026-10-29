"""Independent check of the number of I-SFG structures per dependency level.

A level structure is a series-parallel (= N-free) partial order on its VNFs.
  1) brute force: every labeled partial order on n <= 5 elements, keep the N-free ones
  2) exponential generating functions of the unique series / parallel decomposition
Both must agree with what model.cpp layerStructures() produces (and selftest checks):
  1, 3, 19, 195, 2791, 51303
Run:  python tests/count_sp.py      (about a minute, the n = 5 brute force dominates)
"""
import itertools
from fractions import Fraction as Fr
from math import factorial


def brute(n):
    pairs = [(i, j) for i in range(n) for j in range(n) if i != j]
    posets = nfree = 0
    for mask in range(1 << len(pairs)):
        R = {p for k, p in enumerate(pairs) if mask >> k & 1}
        if any((j, i) in R for (i, j) in R):
            continue
        if any((i, k) not in R for (i, j) in R for (j2, k) in R if j == j2 and i != k):
            continue
        posets += 1
        lt = lambda a, b: (a, b) in R
        inc = lambda a, b: a != b and not lt(a, b) and not lt(b, a)
        if not any(lt(a, c) and lt(b, c) and lt(b, d) and inc(a, d) and inc(a, b) and inc(c, d)
                   for a, b, c, d in itertools.permutations(range(n), 4)):
            nfree += 1
    return posets, nfree


def egf(N=8):
    def mul(a, b):
        return [sum(a[i] * b[k - i] for i in range(k + 1)) for k in range(N)]

    def exp_(a):
        r = [Fr(0)] * N; r[0] = Fr(1); term = r[:]
        for k in range(1, N):
            term = [t / k for t in mul(term, a)]
            r = [x + y for x, y in zip(r, term)]
        return r

    X = [Fr(0)] * N; X[1] = Fr(1)
    S = [Fr(0)] * N; P = [Fr(0)] * N
    for _ in range(N + 2):
        NS = [x + p for x, p in zip(X, P)]      # not a series: one VNF or a parallel part
        NP = [x + s for x, s in zip(X, S)]      # not a parallel: one VNF or a series part
        pw = mul(NS, NS); S = [Fr(0)] * N       # series: ordered, >= 2 parts
        while any(pw):
            S = [a + b for a, b in zip(S, pw)]; pw = mul(pw, NS)
        e = exp_(NP)                            # parallel: a set of >= 2 parts
        P = [e[k] - (1 if k == 0 else 0) - NP[k] for k in range(N)]
    return [int((X[k] + S[k] + P[k]) * factorial(k)) for k in range(1, N)]


if __name__ == "__main__":
    print("EGF:", egf())
    for n in range(1, 6):
        print("n =", n, "labeled posets / series-parallel:", brute(n))
