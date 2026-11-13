#include <string>
#include <vector>

#include "interpreter/CostModel.hpp"
#include "interpreter/Interpreter.hpp"
#include "language/Parser.hpp"
#include "language/PrettyPrinter.hpp"
#include "problems/ReferenceProblem.hpp"
#include "superopt/CFrontend.hpp"
#include "superopt/CompiledTimer.hpp"
#include "superopt/Superoptimizer.hpp"
#include "test_framework.hpp"

using namespace algosearch;

TEST(cost_model_weights_executed_nodes) {
    Program p = parseProgram("arr[0] = (arr[0] * 3);\n");
    CHECK(p != nullptr);
    std::vector<int> out;
    Interpreter interp(1000);
    CostModel scalar = CostModel::x86Default(true);
    interp.setCostModel(&scalar);
    CHECK(interp.run(*p, {5}, 2, out) == RunStatus::COMPLETED);
    CHECK_EQ(Interpreter::lastCost(), 3LL);  // arguments live in registers: only the multiply
    CostModel array = CostModel::x86Default(false);
    interp.setCostModel(&array);
    interp.run(*p, {5}, 2, out);
    CHECK_EQ(Interpreter::lastCost(), 5LL);  // load + multiply + store
    // A loop pays its body and one branch per condition check.
    Program loop = parseProgram("v1 = 0;\nwhile ((v1 < 3)) {\n  v1 = (v1 + 1);\n}\n");
    interp.run(*loop, {0}, 2, out);
    CHECK_EQ(Interpreter::lastCost(), 4LL * 2 + 3LL * 1);  // 4 checks: compare + branch; 3 adds
}

TEST(cost_model_does_not_change_a_run) {
    Program p = parseProgram("v1 = arr[0];\nwhile ((v1 > 0)) {\n  v2 = (v2 + (v1 % 7));\n  v1 = (v1 - 1);\n}\narr[0] = v2;\n");
    CostModel m = CostModel::x86Default(false);
    std::vector<int> a, b;
    long sa = 0, sb = 0;
    Interpreter plain(100000), costed(100000);
    costed.setCostModel(&m);
    CHECK(plain.run(*p, {40}, 3, a, &sa) == RunStatus::COMPLETED);
    CHECK(costed.run(*p, {40}, 3, b, &sb) == RunStatus::COMPLETED);
    CHECK(a == b);
    CHECK_EQ(sa, sb);
    plain.run(*p, {40}, 3, a);
    CHECK_EQ(Interpreter::lastCost(), 0LL);  // no model: nothing accounted
}

TEST(superopt_mutation_is_deterministic_per_seed_and_edits) {
    Program p = parseProgram("v1 = arr[0];\nif ((v1 < 0)) {\n  v1 = (0 - v1);\n}\narr[0] = v1;\n");
    std::string base = printStmt(*p);
    int changed = 0;
    for (uint64_t seed = 1; seed <= 50; ++seed) {
        Program a = mutateForSuperopt(*p, 4, 1, seed), b = mutateForSuperopt(*p, 4, 1, seed);
        CHECK(printStmt(*a) == printStmt(*b));
        changed += printStmt(*a) != base;
    }
    CHECK(changed >= 40);
    CHECK(printStmt(*p) == base);  // the parent is never modified
}

TEST(superopt_removes_redundant_work_and_stays_equivalent) {
    ReferenceProblem problem(
        importCFunction("int f(int x) { int y = x * 1; int z = y + 0; int w = z * 2; w = w - x; return w / 1; }"), 6,
        3, 1'000'000);
    SuperoptConfig cfg;
    cfg.wallSeconds = 3.0;
    cfg.seed = 3;
    Superoptimizer opt(problem, cfg);
    SuperoptResult r = opt.run();
    CHECK(r.verified);
    CHECK(r.bestCost < r.referenceCost);
    CHECK(r.bestCost <= r.referenceCost / 4);  // x * 1, + 0, * 2, / 1 are all removable: x + x - x
    Interpreter interp(10000);
    interp.setWrapArithmetic(true);
    std::vector<int> out;
    for (int x : {0, 1, -1, 12345, -98765, 2147483647, -2147483647 - 1}) {
        CHECK(interp.run(*r.best, {x}, problem.numVars(), out) == RunStatus::COMPLETED);
        CHECK_EQ(out[0], x);
    }
}

TEST(superopt_never_returns_a_program_worse_than_the_reference) {
    ReferenceProblem problem(importCFunction("int f(int a, int b) { return a > b ? a : b; }"), 5, 1, 1'000'000);
    SuperoptConfig cfg;
    cfg.wallSeconds = 1.0;
    Superoptimizer opt(problem, cfg);
    SuperoptResult r = opt.run();
    CHECK(r.verified);
    CHECK(r.bestCost <= r.referenceCost);
    CHECK_EQ(r.referenceCost, opt.cost(problem.referenceProgram()));
}

TEST(compiled_timer_self_control_is_near_one_and_reports_failures) {
    std::string original = "int f(int x) { int c = 0; while (x != 0) { x &= x - 1; c++; } return c; }\n";
    CImport imp = importCFunction(original);
    std::vector<std::vector<int>> workload;
    for (int i = 0; i < 64; ++i) workload.push_back({i * 2654435761u > 0 ? static_cast<int>(i * 2654435761u) : i});
    CompiledTimer timer(original, imp.abi, workload, "/tmp/algosearch_timer_test");
    CompiledTimer::Timing self = timer.time(original);
    CHECK(self.ok);
    CHECK(self.speedup > 0.9 && self.speedup < 1.1);
    // A different program (the count bit by bit) is timed against the same baseline.
    std::string slow = "int f(int x) { int c = 0; for (int i = 0; i < 32; i++) c += (x >> i) & 1; return c; }\n";
    CompiledTimer::Timing t = timer.time(slow);
    CHECK(t.ok);
    CHECK(t.referenceNs > 0);
    CHECK(t.speedup != self.speedup);
    // An uncompilable candidate is reported, not a crash.
    CompiledTimer::Timing bad = timer.time("int f(int x) { return x +; }\n");
    CHECK(!bad.ok);
    CHECK(!bad.error.empty());
}

TEST(timed_superopt_returns_the_reference_unless_measurably_faster) {
    std::string src = "int f(int a, int b) { return a > b ? a : b; }\n";
    ReferenceProblem problem(importCFunction(src), 5, 1, 1'000'000);
    SuperoptConfig cfg;
    cfg.wallSeconds = 3.0;
    cfg.timeTopK = 2;
    cfg.originalSource = src;
    cfg.workDir = "/tmp/algosearch_timed_superopt_test";
    Superoptimizer opt(problem, cfg);
    SuperoptResult r = opt.run();
    CHECK(r.verified);
    CHECK(r.timings >= 4);  // self-control x3 + emitted reference
    CHECK(r.selfControl > 0.8 && r.selfControl < 1.25);
    CHECK(r.noiseThreshold >= 0.03);
    // Either no gain (the reference is returned) or a gain above the noise.
    bool isReference = printStmt(*r.best) == printStmt(problem.referenceProgram());
    CHECK(isReference ? r.measuredSpeedup == 1.0 : r.measuredSpeedup > 1.0 + r.noiseThreshold);
}
