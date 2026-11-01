#include "problems/FreshProblem.hpp"

#include <algorithm>

#include "core/Random.hpp"

namespace algosearch {

namespace {

std::vector<int> randomVector(Random& rng, int minLen, int maxLen, int lo, int hi) {
    std::vector<int> v(static_cast<size_t>(rng.uniformInt(minLen, maxLen)));
    for (int& x : v) x = rng.uniformInt(lo, hi);
    return v;
}

}  // namespace

std::string FreshProblem::name(Kind kind) {
    switch (kind) {
        case Kind::SumMultiples3Or5: return "sum_multiples_3_or_5";
        case Kind::Binomial: return "binomial";
        case Kind::DigitSum: return "digit_sum";
        case Kind::CumulativeMax: return "cumulative_max";
        case Kind::Popcount: return "popcount";
        case Kind::AlternatingSum: return "alternating_sum";
        case Kind::IsPowerOfTwo: return "is_power_of_two";
        case Kind::BalancedParentheses: return "balanced_parentheses";
        case Kind::SwapAdjacentPairs: return "swap_adjacent_pairs";
        case Kind::CountPairsWithSum: return "count_pairs_with_sum";
        case Kind::MatrixTranspose: return "matrix_transpose";
        case Kind::BinaryToDecimal: return "binary_to_decimal";
        case Kind::RangeMaxMin: return "range_max_min";
        case Kind::AdjacentDifferences: return "adjacent_differences";
        case Kind::Lcm: return "lcm";
        case Kind::MaxNonAdjacentSum: return "max_non_adjacent_sum";
        case Kind::Isqrt: return "isqrt";
        case Kind::CountDivisors: return "count_divisors";
        case Kind::KthSmallest: return "kth_smallest";
        case Kind::Josephus: return "josephus";
    }
    return "";
}

std::vector<int> FreshProblem::reference(Kind kind, const std::vector<int>& arr) {
    switch (kind) {
        case Kind::SumMultiples3Or5: {
            int n = arr.empty() ? 0 : arr[0], sum = 0;
            for (int k = 1; k < n; ++k) {
                if (k % 3 == 0 || k % 5 == 0) sum += k;
            }
            return {sum};
        }
        case Kind::Binomial: {
            if (arr.size() < 2) return {0};
            int n = arr[0], k = arr[1];
            long c = 1;
            for (int i = 1; i <= k; ++i) c = c * (n - k + i) / i;
            return {static_cast<int>(c)};
        }
        case Kind::DigitSum: {
            int n = arr.empty() ? 0 : arr[0], sum = 0;
            while (n > 0) {
                sum += n % 10;
                n /= 10;
            }
            return {sum};
        }
        case Kind::CumulativeMax: {
            std::vector<int> out = arr;
            for (size_t i = 1; i < out.size(); ++i) out[i] = std::max(out[i], out[i - 1]);
            return out;
        }
        case Kind::Popcount: {
            int n = arr.empty() ? 0 : arr[0], count = 0;
            while (n > 0) {
                count += n & 1;
                n >>= 1;
            }
            return {count};
        }
        case Kind::AlternatingSum: {
            int sum = 0;
            for (size_t i = 0; i < arr.size(); ++i) sum += i % 2 == 0 ? arr[i] : -arr[i];
            return {sum};
        }
        case Kind::IsPowerOfTwo: {
            int n = arr.empty() ? 0 : arr[0];
            return {n > 0 && (n & (n - 1)) == 0 ? 1 : 0};
        }
        case Kind::BalancedParentheses: {
            int depth = 0;
            for (int x : arr) {
                depth += x;
                if (depth < 0) return {0};
            }
            return {depth == 0 ? 1 : 0};
        }
        case Kind::SwapAdjacentPairs: {
            std::vector<int> out = arr;
            for (size_t i = 0; i + 1 < out.size(); i += 2) std::swap(out[i], out[i + 1]);
            return out;
        }
        case Kind::CountPairsWithSum: {
            if (arr.empty()) return {0};
            int count = 0;
            for (size_t i = 1; i < arr.size(); ++i) {
                for (size_t j = i + 1; j < arr.size(); ++j) count += arr[i] + arr[j] == arr[0];
            }
            return {count};
        }
        case Kind::MatrixTranspose: {
            size_t n = 0;
            while ((n + 1) * (n + 1) <= arr.size()) ++n;
            std::vector<int> out = arr;
            for (size_t i = 0; i < n; ++i) {
                for (size_t j = 0; j < n; ++j) out[i * n + j] = arr[j * n + i];
            }
            return out;
        }
        case Kind::BinaryToDecimal: {
            int value = 0;
            for (int bit : arr) value = value * 2 + bit;
            return {value};
        }
        case Kind::RangeMaxMin: {
            if (arr.empty()) return {0};
            return {*std::max_element(arr.begin(), arr.end()) - *std::min_element(arr.begin(), arr.end())};
        }
        case Kind::AdjacentDifferences: {
            std::vector<int> out = arr;
            for (size_t i = 1; i < out.size(); ++i) out[i] = arr[i] - arr[i - 1];
            return out;
        }
        case Kind::Lcm: {
            if (arr.size() < 2 || arr[0] <= 0 || arr[1] <= 0) return {0};
            int a = arr[0], b = arr[1];
            while (b != 0) {
                int t = a % b;
                a = b;
                b = t;
            }
            return {arr[0] / a * arr[1]};
        }
        case Kind::MaxNonAdjacentSum: {
            int take = 0, skip = 0;
            for (int x : arr) {
                int newTake = skip + x;
                skip = std::max(skip, take);
                take = newTake;
            }
            return {std::max(take, skip)};
        }
        case Kind::Isqrt: {
            int n = arr.empty() ? 0 : arr[0], r = 0;
            while ((r + 1) * (r + 1) <= n) ++r;
            return {r};
        }
        case Kind::CountDivisors: {
            int n = arr.empty() ? 0 : arr[0], count = 0;
            for (int d = 1; d <= n; ++d) count += n % d == 0;
            return {count};
        }
        case Kind::KthSmallest: {
            if (arr.size() < 2) return {0};
            std::vector<int> items(arr.begin() + 1, arr.end());
            std::sort(items.begin(), items.end());
            size_t k = static_cast<size_t>(std::max(1, std::min(arr[0], static_cast<int>(items.size()))));
            return {items[k - 1]};
        }
        case Kind::Josephus: {
            if (arr.size() < 2) return {0};
            int survivor = 0;
            for (int i = 2; i <= arr[0]; ++i) survivor = (survivor + arr[1]) % i;
            return {survivor + 1};
        }
    }
    return {0};
}

std::vector<int> FreshProblem::randomInput(Kind kind, Random& rng) {
    switch (kind) {
        case Kind::SumMultiples3Or5: return {rng.uniformInt(1, 100)};
        case Kind::Binomial: {
            int n = rng.uniformInt(1, 12);
            return {n, rng.uniformInt(0, n)};
        }
        case Kind::DigitSum: return {rng.uniformInt(1, 99999)};
        case Kind::CumulativeMax:
        case Kind::AlternatingSum:
        case Kind::SwapAdjacentPairs: return randomVector(rng, 1, 8, -20, 20);
        case Kind::Popcount: return {rng.uniformInt(0, 1023)};
        case Kind::IsPowerOfTwo:
            if (rng.uniformInt(0, 1) == 0) return {1 << rng.uniformInt(0, 9)};
            return {rng.uniformInt(1, 1000)};
        case Kind::BalancedParentheses: {
            if (rng.uniformInt(0, 1) == 0) {
                // A balanced sequence: open while opens remain (always when the stack is empty, else by coin), else close.
                int pairs = rng.uniformInt(1, 5), opens = 0, depth = 0;
                std::vector<int> v;
                while (static_cast<int>(v.size()) < 2 * pairs) {
                    bool open = opens < pairs && (depth == 0 || rng.uniformInt(0, 1) == 0);
                    v.push_back(open ? 1 : -1);
                    depth += open ? 1 : -1;
                    opens += open;
                }
                return v;
            }
            std::vector<int> v(static_cast<size_t>(rng.uniformInt(1, 10)));
            for (int& x : v) x = rng.uniformInt(0, 1) == 0 ? 1 : -1;
            return v;
        }
        case Kind::CountPairsWithSum: {
            std::vector<int> v = randomVector(rng, 2, 8, -5, 5);
            v.insert(v.begin(), rng.uniformInt(-10, 10));
            return v;
        }
        case Kind::MatrixTranspose: {
            int n = rng.uniformInt(2, 3);
            return randomVector(rng, n * n, n * n, -20, 20);
        }
        case Kind::BinaryToDecimal: return randomVector(rng, 1, 8, 0, 1);
        case Kind::RangeMaxMin:
        case Kind::AdjacentDifferences: return randomVector(rng, 1, 8, -20, 20);
        case Kind::Lcm: return {rng.uniformInt(1, 30), rng.uniformInt(1, 30)};
        case Kind::MaxNonAdjacentSum: return randomVector(rng, 1, 8, 0, 20);
        case Kind::Isqrt: return {rng.uniformInt(0, 200)};
        case Kind::CountDivisors: return {rng.uniformInt(1, 100)};
        case Kind::KthSmallest: {
            std::vector<int> v = randomVector(rng, 1, 8, -20, 20);
            v.insert(v.begin(), rng.uniformInt(1, static_cast<int>(v.size())));
            return v;
        }
        case Kind::Josephus: return {rng.uniformInt(1, 12), rng.uniformInt(1, 5)};
    }
    return {0};
}

FreshProblem::FreshProblem(Kind kind, int numVars, uint64_t seed) : kind_(kind), numVars_(numVars) {
    Random rng(seed);
    auto make = [&](size_t count, std::vector<Instance>& out) {
        while (out.size() < count) {
            Instance inst;
            inst.input = randomInput(kind_, rng);
            inst.expectedOutput = inst.input;
            std::vector<int> ref = reference(kind_, inst.input);
            for (size_t i = 0; i < ref.size(); ++i) inst.expectedOutput[i] = ref[i];
            out.push_back(std::move(inst));
        }
    };
    make(200, train_);
    make(2000, test_);
}

std::string FreshProblem::describeLevel(int level) const {
    (void)level;
    return "learning-curve pool (section 149) " + name(kind_) + ", " + std::to_string(train_.size()) + " training cases";
}

bool FreshProblem::isSolution(const std::vector<int>& input, const std::vector<int>& output) const {
    if (output.size() != input.size()) return false;
    std::vector<int> ref = reference(kind_, input);
    for (size_t i = 0; i < ref.size(); ++i) {
        if (output[i] != ref[i]) return false;
    }
    return true;
}

double FreshProblem::partialScore(const std::vector<int>& input, const std::vector<int>& output) const {
    if (output.size() != input.size()) return 0.0;
    std::vector<int> ref = reference(kind_, input);
    size_t good = 0;
    for (size_t i = 0; i < ref.size(); ++i) good += output[i] == ref[i];
    return static_cast<double>(good) / static_cast<double>(ref.size());
}

Instance FreshProblem::generateBenchmarkInstance(int n, uint64_t seed) const {
    (void)n;
    if (train_.empty()) return {{}, {}};
    return train_[seed % train_.size()];
}

}  // namespace algosearch
