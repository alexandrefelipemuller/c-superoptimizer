#!/usr/bin/env python3
"""Section 155: size of the joint hole space of a loop skeleton whose update expression is enumerated.

Expressions with at most n binary ops (+ - * / %) over L leaves; the joint space multiplies the holes of the known solution
(the update expression, the final store expression) by the small discrete choices (initial constants in 0..3, comparator).
This is a counting estimate under stated assumptions, not a measurement: it assumes the right skeleton is given.
"""
def exprs(L, n, O=5):
    E = [L]
    for k in range(1, n + 1):
        E.append(O * sum(E[i] * E[k - 1 - i] for i in range(k)))
    return sum(E[:n + 1])

consts = 4  # literals 0..3
P = {
    'josephus':               ([(3 + consts, 2), (1 + consts, 1)], 4 * 4 * 2),
    'lcm':                    ([(3 + consts, 2), (3 + consts, 1)], 2 * 2),
    'factorial_trailing_zeros': ([(2 + consts + 1, 1), (2 + consts, 1)], 2 * 2),
    'prefix_sums':            ([(2 + consts, 2)], 4 * 2),
    'adjacent_differences':   ([(2 + consts, 2)], 4 * 2 * 2),
    'digital_root':           ([(1 + consts + 1, 3)], 1),
    'reverse_number':         ([(3 + consts + 1, 3)], 4 * 2),
    'dot_product_halves':     ([(5 + consts, 3), (3 + consts, 1)], 4 * 2),
}
budget = 300000  # candidates scored in a 60M-execution run with 200 training cases
for k, (holes, mult) in P.items():
    tot = mult
    for L, n in holes:
        tot *= exprs(L, n)
    print("%-26s joint fills %.2e  budget/fills %.3f  (100x cheaper with a 3-case prefilter: %.3f)"
          % (k, tot, min(1, budget / tot), min(1, 100 * budget / tot)))
