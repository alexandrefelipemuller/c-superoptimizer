#pragma once

#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "interpreter/Interpreter.hpp"
#include "problems/Problem.hpp"
#include "superopt/CFrontend.hpp"

namespace algosearch {

// The superoptimizer prototype's problem (docs/superopt.md, phase 2): the
// specification is a C function imported with importCFunction, and the
// expected result of every input is what that reference program computes
// (run in wraparound mode, the gcc -fwrapv model). Selected with
// `--problem c:FILE.c[:func]`.
//
// Domain: an input on which the reference hits a runtime error or its step
// limit (division by zero, an out-of-bounds read, ...) is undefined
// behaviour in C, so it is outside the function's domain -- it never
// becomes an instance, and isSolution accepts any output on it.
//
// Levels. Array ABI: level n = array length, as in the array problems
// (level 1 also holds the empty array, when it is in the domain).
// Scalar ABI: level L draws every argument from [-M, M] with
// M = 2^(L+1) - 1 (L = 1: [-3, 3]; L = 7: [-255, 255]; L >= 30: all of
// int, INT_MIN included), exhaustive while the grid is small.
class ReferenceProblem : public Problem {
public:
    // `referenceStepLimit`: inputs on which the reference runs longer are
    // outside the domain (default 10M interpreter steps).
    ReferenceProblem(CImport reference, int numVars, uint64_t seed, long referenceStepLimit = 10'000'000);

    // Reads and imports FILE (throws CImportError on unsupported C, and
    // std::runtime_error when the file cannot be read).
    static std::unique_ptr<ReferenceProblem> fromFile(const std::string& path, const std::string& func, int numVars,
                                                      uint64_t seed);

    std::vector<Instance> generateInstances(int level) const override;
    std::string describeLevel(int level) const override;
    int nextLevel(int level) const override { return level + 1; }
    std::vector<Instance> generateAdversarialInstances(int level) const override;
    std::vector<std::vector<int>> exhaustiveInputs(int n) const override;
    bool isSolution(const std::vector<int>& input, const std::vector<int>& output) const override;
    double partialScore(const std::vector<int>& input, const std::vector<int>& output) const override;
    int numVars() const override;
    Instance generateBenchmarkInstance(int n, uint64_t seed) const override;
    bool wrapArithmetic() const override { return true; }

    const CFunctionAbi& abi() const { return abi_; }
    const Stmt& referenceProgram() const { return *reference_; }

    // The reference's result on `input`, or nullopt outside its domain.
    std::optional<std::vector<int>> expected(const std::vector<int>& input) const;
    // Interpreter steps the reference takes on `input`, or -1 outside its
    // domain.
    long referenceSteps(const std::vector<int>& input) const;

    static int magnitude(int level);

private:
    std::vector<Instance> toInstances(const std::vector<std::vector<int>>& inputs) const;
    std::vector<std::vector<int>> scalarGrid(int lo, int hi) const;

    Program reference_;
    CFunctionAbi abi_;
    int numVars_;
    uint64_t seed_;
    Interpreter referenceInterp_;

    struct VecHash {
        size_t operator()(const std::vector<int>& v) const;
    };
    mutable std::shared_mutex cacheMutex_;
    struct Run {
        std::optional<std::vector<int>> result;
        long steps = -1;
    };
    Run reference(const std::vector<int>& input) const;  // cached
    mutable std::unordered_map<std::vector<int>, Run, VecHash> cache_;
};

}  // namespace algosearch
