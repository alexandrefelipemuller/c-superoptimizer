#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ast/Stmt.hpp"
#include "interpreter/CostModel.hpp"
#include "problems/ReferenceProblem.hpp"

namespace algosearch {

// Phase 3 of the superoptimizer prototype (docs/superopt.md): start from the
// reference program and search, by local mutation, for an equivalent program
// with a lower weighted cost (CostModel) on a fixed workload. STOKE-like:
// a beam of correct programs ranked by cost, plus an exploration beam of
// nearly correct ones (few failed tests) so the search can cross incorrect
// intermediates. Correct = passes every test; every new best is then
// checked against exhaustive, adversarial and random inputs, and a
// counterexample becomes a new test (CEGIS).
struct SuperoptConfig {
    double wallSeconds = 30.0;
    int beamWidth = 40;
    int exploreWidth = 40;
    int mutantsPerParent = 12;
    uint64_t seed = 1;
    // Array functions: test levels (lengths) 1..arrayMaxN.
    int arrayMaxN = 10;
    // An exploration candidate may fail at most this fraction of the tests.
    double maxFailFraction = 0.1;
    bool verbose = false;
};

struct SuperoptResult {
    Program best;
    long long referenceCost = 0;
    long long bestCost = 0;
    long evaluated = 0;
    long correct = 0;
    int counterexamples = 0;  // CEGIS: inputs added to the tests
    int tests = 0;
    int workload = 0;
    bool verified = false;  // verifyGeneralization on the final best
    std::string verifyReason;
    // (seconds, cost) each time the best improved.
    std::vector<std::pair<double, long long>> trajectory;
};

class Superoptimizer {
public:
    Superoptimizer(const ReferenceProblem& problem, SuperoptConfig config);

    SuperoptResult run();

    // Weighted cost of `program` summed over the workload; -1 when it does
    // not complete on some workload input.
    long long cost(const Stmt& program) const;

    const std::vector<Instance>& tests() const { return tests_; }

    // A candidate may take this many steps on an input the reference
    // finishes in `referenceSteps`.
    static long stepBudget(long referenceSteps) { return 4 * referenceSteps + 200; }

private:
    const ReferenceProblem& problem_;
    SuperoptConfig config_;
    CostModel model_;
    std::vector<Instance> tests_;
    std::vector<long> testLimits_;  // stepBudget per test
    std::vector<std::vector<int>> workload_;
    std::vector<long> workloadLimits_;
};

// One random local edit of `program` (exposed for tests). `numVars` bounds
// the variables a new expression may read or write; `arity` is the number
// of scalar arguments (0 for an array function).
Program mutateForSuperopt(const Stmt& program, int numVars, int arity, uint64_t seed);

}  // namespace algosearch
