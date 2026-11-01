#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "problems/Problem.hpp"

namespace algosearch {

// Section 149: the learning-curve pool. Thirty problems drawn from areas the 77-problem suite covers little, in three
// batches of ten fixed by a registered random draw (batch 1 here; later batches are added only after the library change
// that follows batch 1). Instance ranges and references were fixed before any search ran. A run gets 200 training cases
// and is judged on 2,000 unseen cases, as for Psb1Problem. The program reads `arr` and writes its answer at the front
// (scalar results) or in place (array results). Names: fresh_<name>.
// Batch 1:
//   sum_multiples_3_or_5: arr = {n}, n in 1..100; arr[0] = the sum of every k in 1..n-1 divisible by 3 or by 5.
//   binomial: arr = {n, k}, n in 1..12, k in 0..n; arr[0] = n choose k.
//   digit_sum: arr = {n}, n in 1..99999; arr[0] = the sum of the decimal digits of n.
//   cumulative_max: arr = 1..8 items in [-20, 20]; the output replaces item i by the maximum of items 0..i.
//   popcount: arr = {n}, n in 0..1023; arr[0] = the number of 1 bits of n.
//   alternating_sum: arr = 1..8 items in [-20, 20]; arr[0] = a0 - a1 + a2 - a3 ...
//   is_power_of_two: arr = {n}, n in 1..1000 (half of the cases are powers of two); arr[0] = 1 if n is a power of two,
//     else 0.
//   balanced_parentheses: arr = 1..10 items, each +1 (open) or -1 (close); arr[0] = 1 if no prefix sum is negative and
//     the total is 0, else 0 (half of the cases are balanced).
//   swap_adjacent_pairs: arr = 1..8 items in [-20, 20]; items (0,1), (2,3), ... are swapped; a last odd item stays.
//   count_pairs_with_sum: arr = {key, a_1 .. a_m}, key in [-10, 10], m in 2..8, items in [-5, 5]; arr[0] = the number of
//     pairs i < j with a_i + a_j = key.
// Batch 2 (section 150):
//   matrix_transpose: arr = n*n items (n in 2..3) in [-20, 20]; the output is the transposed matrix in place (row-major).
//   binary_to_decimal: arr = 1..8 bits, most significant first; arr[0] = the value.
//   range_max_min: arr = 1..8 items in [-20, 20]; arr[0] = max - min.
//   adjacent_differences: arr = 1..8 items in [-20, 20]; in place a_i := a_i - a_(i-1) for i >= 1, a_0 unchanged.
//   lcm: arr = {a, b}, 1..30; arr[0] = the least common multiple.
//   max_non_adjacent_sum: arr = 1..8 items in [0, 20]; arr[0] = the largest sum of items no two of which are adjacent.
//   isqrt: arr = {n}, n in 0..200; arr[0] = floor(sqrt(n)).
//   count_divisors: arr = {n}, n in 1..100; arr[0] = the number of divisors of n.
//   kth_smallest: arr = {k, a_1 .. a_m}, m in 1..8, items in [-20, 20], k in 1..m; arr[0] = the k-th smallest item.
//   josephus: arr = {n, k}, n in 1..12, k in 1..5; arr[0] = the 1-based survivor of the Josephus problem.
class FreshProblem : public Problem {
public:
    enum class Kind {
        SumMultiples3Or5, Binomial, DigitSum, CumulativeMax, Popcount, AlternatingSum, IsPowerOfTwo,
        BalancedParentheses, SwapAdjacentPairs, CountPairsWithSum,
        // Batch 2 (section 150; encodings fixed before implementation)
        MatrixTranspose, BinaryToDecimal, RangeMaxMin, AdjacentDifferences, Lcm, MaxNonAdjacentSum, Isqrt, CountDivisors,
        KthSmallest, Josephus
    };

    FreshProblem(Kind kind, int numVars, uint64_t seed);

    static std::string name(Kind kind);
    static std::vector<int> reference(Kind kind, const std::vector<int>& arr);
    static std::vector<int> randomInput(Kind kind, class Random& rng);

    std::vector<Instance> generateInstances(int level) const override { (void)level; return train_; }
    std::string describeLevel(int level) const override;
    int nextLevel(int level) const override { return level + 1; }
    std::vector<Instance> generateAdversarialInstances(int level) const override { (void)level; return test_; }
    bool supportsMagnitudeShift() const override { return false; }
    bool isSolution(const std::vector<int>& input, const std::vector<int>& output) const override;
    double partialScore(const std::vector<int>& input, const std::vector<int>& output) const override;
    int numVars() const override { return numVars_; }
    Instance generateBenchmarkInstance(int n, uint64_t seed) const override;

private:
    Kind kind_;
    int numVars_;
    std::vector<Instance> train_, test_;
};

}  // namespace algosearch
