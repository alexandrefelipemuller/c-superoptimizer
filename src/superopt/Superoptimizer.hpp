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
    // Phase 3b: compile and time candidates against the original inside the
    // loop (CompiledTimer). 0 = off: the model alone decides (phase 3).
    int timeTopK = 0;
    std::string originalSource;  // the user's C file, the timing baseline
    std::string workDir = "/tmp/algosearch_superopt";
    int timedParents = 6;         // fastest timed programs used as parents
    double timeFraction = 0.5;    // at most this share of the wall clock goes to timing
    int timingInputs = 512;       // fresh inputs for compiled timing (section 190: 24 let branches be memorized)
    // Phase 3c (log 190/190b): algorithmic moves in a quarter of the edits; default
    // since 190b (`--no-algorithmic-moves` restores phase 3b).
    bool algorithmicMoves = true;
    // Log 195: the user's typical inputs (`--workload FILE`). When given, the
    // compiled timing and the model cost run on them (the in-domain ones)
    // instead of random inputs, and they join the tests. Correctness is still
    // checked on every input (tests, CEGIS, boundaries, exhaustive gate).
    std::vector<std::vector<int>> userWorkload;
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
    int userWorkload = 0;  // in-domain inputs of --workload used (0: random workloads)
    bool verified = false;  // verifyGeneralization on the final best
    std::string verifyReason;
    // (seconds, cost) each time the best improved.
    std::vector<std::pair<double, long long>> trajectory;
    // Phase 3b (timeTopK > 0).
    int timings = 0;
    double noiseThreshold = 0;     // a speedup must exceed 1 + this to count
    double selfControl = 1.0;      // original against itself, median of 3
    double emittedReference = 1.0; // the imported reference, emitted, against the original
    double measuredSpeedup = 1.0;  // of `best`, re-measured; 1.0 when best is the reference
    double timingSeconds = 0;
    // How the returned program's equivalence was established (log 192):
    // "reference" (nothing changed), "exhaustive" (unary: all 2^32 arguments,
    // compiled) or "tested" (tests + CEGIS + boundary inputs only).
    std::string equivalence = "reference";
    int exhaustiveRejected = 0;  // timed winners refuted by the exhaustive check
    Program modelBest;             // what the model alone would have returned
    long long modelBestCost = 0;
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
    std::vector<std::vector<int>> userWorkload_;  // config_.userWorkload, in-domain inputs only
    std::vector<long> testLimits_;  // stepBudget per test
    std::vector<std::vector<int>> workload_;
    std::vector<long> workloadLimits_;
};

// One random local edit of `program` (exposed for tests). `numVars` bounds
// the variables a new expression may read or write; `arity` is the number
// of scalar arguments (0 for an array function). `algorithmic` adds the
// phase 3c moves (loop unrolling, if -> branch-free select, early exits,
// loop -> if, strength reduction by powers of two) to a quarter of the
// edits; off, the edits are exactly those of phase 3.
Program mutateForSuperopt(const Stmt& program, int numVars, int arity, uint64_t seed, bool algorithmic = false,
                          bool returnsValue = true);

// One specific phase 3c move (0 unroll, 1 branch-free select, 2 early exit,
// 3 loop -> if, 4 strength reduction), for tests; null when it does not apply.
Program applyAlgorithmicMove(const Stmt& program, int move, int numVars, int arity, uint64_t seed,
                             bool returnsValue = true);

}  // namespace algosearch
