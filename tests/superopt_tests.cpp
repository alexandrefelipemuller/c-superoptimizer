#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "core/Random.hpp"
#include "interpreter/CostModel.hpp"
#include "interpreter/Interpreter.hpp"
#include "language/Parser.hpp"
#include "language/PrettyPrinter.hpp"
#include "problems/ReferenceProblem.hpp"
#include "search/Benchmark.hpp"
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

namespace {

// Final arrays of `a` and `b` agree on every input (both run in wraparound mode).
bool sameBehaviour(const Stmt& a, const Stmt& b, const std::vector<std::vector<int>>& inputs, int numVars) {
    Interpreter interp(1'000'000);
    interp.setWrapArithmetic(true);
    std::vector<int> oa, ob;
    for (const auto& in : inputs) {
        RunStatus ra = interp.run(a, in, numVars, oa);
        RunStatus rb = interp.run(b, in, numVars, ob);
        if (ra != rb || (ra == RunStatus::COMPLETED && oa != ob)) return false;
    }
    return true;
}

}  // namespace

TEST(superopt_unroll_move_preserves_behaviour) {
    Program loop = parseProgram(
        "v1 = arr[0];\nv2 = 0;\nwhile (v1) {\n  if ((v1 == 7)) {\n    continue;\n  }\n  v1 = (v1 & (v1 - 1));\n"
        "  v2 = (v2 + 1);\n}\narr[0] = v2;\n");
    CHECK(loop != nullptr);
    Program un = applyAlgorithmicMove(*loop, 0, 4, 1, 1);
    CHECK(un != nullptr);
    CHECK(un->size() > loop->size() + 8);
    std::vector<std::vector<int>> inputs;
    for (int x : {0, 1, 2, 3, 6, 8, 255, 1 << 20, -1, -8, 123456789}) inputs.push_back({x});
    CHECK(sameBehaviour(*loop, *un, inputs, 4));  // `continue` (an infinite loop at 7) too: both time out
}

TEST(superopt_branch_free_move_gives_a_select) {
    Program absIf = parseProgram("v1 = arr[0];\nif ((v1 < 0)) {\n  v1 = (0 - v1);\n}\narr[0] = v1;\n");
    Program sel = applyAlgorithmicMove(*absIf, 1, 4, 1, 1);
    CHECK(sel != nullptr);
    CHECK(printStmt(*sel).find("if") == std::string::npos);
    std::vector<std::vector<int>> inputs;
    for (int x : {0, 1, -1, 5, -5, 2147483647, -2147483647 - 1}) inputs.push_back({x});
    CHECK(sameBehaviour(*absIf, *sel, inputs, 4));
    Program maxIf = parseProgram("if ((arr[0] > arr[1])) {\n  v1 = arr[0];\n} else {\n  v1 = arr[1];\n}\narr[0] = v1;\n");
    Program sel2 = applyAlgorithmicMove(*maxIf, 1, 4, 2, 1);
    CHECK(sel2 != nullptr);
    std::vector<std::vector<int>> pairs = {{1, 2}, {2, 1}, {-3, -3}, {2147483647, -2147483647 - 1}, {-7, 9}};
    CHECK(sameBehaviour(*maxIf, *sel2, pairs, 4));
}

TEST(superopt_strength_reduction_and_loop_to_if_moves) {
    Program mul = parseProgram("arr[0] = ((arr[0] * 8) + (arr[0] % 4));\n");
    for (uint64_t seed = 1; seed <= 6; ++seed) {
        Program r = applyAlgorithmicMove(*mul, 4, 4, 1, seed);
        CHECK(r != nullptr);
        std::string t = printStmt(*r);
        CHECK(t.find("<< 3") != std::string::npos || t.find("& 3") != std::string::npos);
    }
    Program w = parseProgram("v1 = arr[0];\nwhile ((v1 > 10)) {\n  v1 = (v1 - 10);\n}\narr[0] = v1;\n");
    Program once = applyAlgorithmicMove(*w, 3, 4, 1, 1);
    CHECK(once != nullptr);
    CHECK(printStmt(*once).find("while") == std::string::npos);
    CHECK(applyAlgorithmicMove(*mul, 0, 4, 1, 1) == nullptr);  // no loop to unroll
}

TEST(superopt_algorithmic_moves_off_leave_phase_3_edits_unchanged) {
    Program p = parseProgram("v1 = arr[0];\nwhile ((v1 > 0)) {\n  v1 = (v1 - 3);\n}\narr[0] = v1;\n");
    for (uint64_t seed = 1; seed <= 30; ++seed)
        CHECK(printStmt(*mutateForSuperopt(*p, 4, 1, seed)) == printStmt(*mutateForSuperopt(*p, 4, 1, seed, false)));
    int differs = 0;
    for (uint64_t seed = 1; seed <= 60; ++seed)
        differs += printStmt(*mutateForSuperopt(*p, 4, 1, seed)) != printStmt(*mutateForSuperopt(*p, 4, 1, seed, true));
    CHECK(differs > 0);
}

TEST(compiled_exhaustive_check_finds_a_single_wrong_argument) {
    std::string original = "int f(int x) { return x > 2147483640 ? 1 : 0; }\n";
    CImport imp = importCFunction(original);
    CompiledTimer timer(original, imp.abi, {{1}, {2}}, "/tmp/algosearch_exhaustive_test");
    CompiledTimer::Exhaustive same = timer.exhaustive("int f(int x) { return x >= 2147483641; }\n");
    CHECK(same.complete);
    CHECK_EQ(same.mismatches, 0LL);
    CompiledTimer::Exhaustive off = timer.exhaustive("int f(int x) { return x > 2147483641; }\n");
    CHECK(off.complete);
    CHECK_EQ(off.mismatches, 1LL);
    CHECK_EQ(off.firstBad, 2147483641);
    CHECK(off.bad == std::vector<int>{2147483641});
    // Unsigned signatures (log 194): both sides are renamed, so they link in one unit.
    std::string u = "#include <stdint.h>\nstatic uint32_t g(uint32_t x) { return x >> 1; }\n";
    CompiledTimer utimer(u, importCFunction(u).abi, {{1}, {2}}, "/tmp/algosearch_exhaustive_test");
    CompiledTimer::Exhaustive ux = utimer.exhaustive("unsigned g(unsigned x) { return (int)x >> 1 & 0x7fffffff; }\n");
    CHECK(ux.complete);
    CHECK_EQ(ux.mismatches, 0LL);
}

TEST(superopt_user_workload_keeps_in_domain_inputs_as_tests) {
    auto make = [] {
        return ReferenceProblem(importCFunction("int f(int m, int k) { return 30 + ((0xad5 >> (m - 1)) & 1) + k; }"), 5,
                                1, 1'000'000);
    };
    ReferenceProblem problem = make();
    SuperoptConfig cfg;
    cfg.userWorkload = {{3, 0}, {0, 0}, {40, 1}, {12, 7}};  // m = 0 and m = 40 shift out of range
    Superoptimizer opt(problem, cfg);
    auto has = [&](std::vector<int> in) {
        for (const auto& t : opt.tests())
            if (t.input == in) return true;
        return false;
    };
    CHECK(has({3, 0}));
    CHECK(has({12, 7}));
    CHECK(!has({0, 0}));
    CHECK(!has({40, 1}));
    SuperoptConfig bad;
    bad.userWorkload = {{0, 0}};
    bool threw = false;
    try {
        ReferenceProblem p2 = make();
        Superoptimizer o2(p2, bad);
    } catch (const CImportError&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(reference_problem_errors_outside_its_domain_are_not_failures) {
    // musl's days_in_month: the shift is undefined outside m in 1..32.
    ReferenceProblem problem(
        importCFunction("static int days_in_month(int m, int is_leap) { if (m==2) return 28+is_leap;"
                        " else return 30+((0xad5>>(m-1))&1); }"),
        5, 1, 1'000'000);
    CHECK(!problem.definesAnswer({0, 0}));
    CHECK(problem.definesAnswer({3, 0}));
    std::string reason;
    CHECK(verifyGeneralization(problem, problem.referenceProgram(), 1'000'000, reason, false));
}

TEST(superopt_branch_merge_move_reaches_the_clamp) {
    // Log 203: memsys3Roundup, if (n <= 12) 12 else ((n + 11) & ~7) - 4: with
    // n clamped to 12 the else expression gives 12, so the if becomes a clamp.
    Program round = parseProgram(
        "v1 = arr[0];\nif ((v1 <= 12)) {\n  arr[0] = 12;\n  return;\n} else {\n  arr[0] = (((v1 + 11) & (7 ^ -1)) - 4);\n"
        "  return;\n}\n");
    CHECK(round != nullptr);
    std::vector<std::vector<int>> inputs;
    for (int x : {-100, -1, 0, 1, 11, 12, 13, 14, 19, 20, 21, 100, 4096, 65537}) inputs.push_back({x});
    int exact = 0;
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        Program m = applyAlgorithmicMove(*round, 5, 2, 1, seed);
        CHECK(m != nullptr);
        if (sameBehaviour(*round, *m, inputs, 2)) {
            ++exact;
            CHECK(printStmt(*m).find("else") == std::string::npos);
        }
    }
    CHECK(exact > 0);
}

TEST(superopt_divisibility_split_move_is_exact) {
    Program leap = parseProgram("v1 = arr[0];\narr[0] = (((v1 % 400) == 0) + ((v1 % 100) != 0));\n");
    std::vector<std::vector<int>> inputs;
    for (int x : {0, 1, 4, 16, 25, 100, 200, 300, 400, 800, 1900, 2000, -400, -100, -25, 2147483647, -2147483647 - 1})
        inputs.push_back({x});
    for (uint64_t seed = 1; seed <= 8; ++seed) {
        Program m = applyAlgorithmicMove(*leap, 6, 2, 1, seed);
        CHECK(m != nullptr);
        CHECK(sameBehaviour(*leap, *m, inputs, 2));
        std::string text = printStmt(*m);
        CHECK(text.find("% 16") != std::string::npos || text.find("% 4)") != std::string::npos);
        CHECK(text.find("% 25") != std::string::npos);
    }
    Program odd = parseProgram("v1 = arr[0];\narr[0] = ((v1 % 9) == 0);\n");
    CHECK(applyAlgorithmicMove(*odd, 6, 2, 1, 1) == nullptr);
}

namespace {

std::string readText(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Which idiom moves (7-10) rewrite the planted sample at `path` into an
// equivalent program; -1 in the result marks a rewrite that changed behaviour.
std::vector<int> idiomMovesThatApply(const std::string& path) {
    CImport imp = importCFunction(readText(path));
    int vars = imp.abi.numVars + 2;
    int arity = static_cast<int>(imp.abi.params.size());
    std::vector<std::vector<int>> inputs;
    Random r(7);
    const int special[] = {0, 1, 2, 3, 7, 10, 15, 16, 17, 255, 256, 4095, 4096, 65535, 1 << 29, (1 << 30) + 1,
                           2147483647, -1, -2, -7, -2147483647 - 1};
    for (int k = 0; k < 600; ++k) {
        std::vector<int> in;
        for (int a = 0; a < arity; ++a)
            in.push_back(k < 21 * 21 ? special[(a == 0 ? k : k / 21) % 21] : static_cast<int>(r.engine()()));
        inputs.push_back(in);
    }
    std::vector<int> out;
    for (int move = 7; move <= 10; ++move) {
        Program m = applyAlgorithmicMove(*imp.program, move, vars, arity, 1);
        if (!m) continue;
        out.push_back(sameBehaviour(*imp.program, *m, inputs, vars) ? move : -1);
    }
    return out;
}

}  // namespace

TEST(superopt_idiom_moves_rewrite_the_planted_dev_shapes_exactly) {
    const std::string d = "tests/c_samples_planted/";
    CHECK(idiomMovesThatApply(d + "p_popcount_shift.c") == std::vector<int>{9});
    CHECK(idiomMovesThatApply(d + "p_parity_shift.c") == std::vector<int>{9});
    CHECK(idiomMovesThatApply(d + "p_reverse32.c") == std::vector<int>{9});
    CHECK(idiomMovesThatApply(d + "p_div_sub.c") == std::vector<int>{7});
    CHECK(idiomMovesThatApply(d + "p_mod_sub.c") == std::vector<int>{7});
    CHECK(idiomMovesThatApply(d + "p_sum_range.c") == std::vector<int>{8});
    CHECK(idiomMovesThatApply(d + "p_next_pow2.c") == std::vector<int>{10});
}

TEST(superopt_idiom_moves_never_change_behaviour_on_held_out_samples) {
    for (const char* s : {"h_popcount_xshift", "h_popcount16", "h_parity8", "h_reverse16", "h_div10_sub",
                          "h_sum_below", "h_prev_pow2", "h_ilog2", "h_sum_squares"}) {
        std::vector<int> moves = idiomMovesThatApply(std::string("tests/c_samples_planted_heldout/") + s + ".c");
        for (int m : moves) CHECK(m != -1);
        if (std::getenv("IDIOM_REPORT")) {
            std::cerr << s << ":";
            for (int m : moves) std::cerr << " " << m;
            std::cerr << "\n";
        }
    }
}

TEST(superopt_nested_move_makes_quadratic_array_loops_linear) {
    for (const char* s : {"d_array_max", "d_array_is_sorted"}) {
        CImport imp = importCFunction(readText(std::string("tests/c_samples_deopt/") + s + ".c"));
        int vars = imp.abi.numVars + 2;
        Program m = applyAlgorithmicMove(*imp.program, 11, vars, 0, 1);
        CHECK(m != nullptr);
        if (!m) continue;
        if (std::getenv("IDIOM_REPORT")) std::cerr << s << ":\n" << printStmt(*m);
        // no while nested in a while any more
        std::string t = printStmt(*m);
        size_t first = t.find("while"), depth = 0;
        for (size_t i = first; i < t.size(); ++i) {
            if (t[i] == '{') ++depth;
            if (t[i] == '}' && depth > 0 && --depth == 0) {
                CHECK(t.substr(first + 5, i - first).find("while") == std::string::npos);
                break;
            }
        }
        Random r(3);
        std::vector<std::vector<int>> inputs = {{}, {5}, {1, 1}, {2, 1}, {1, 2, 2, 3}, {3, 3, 3}, {-1, -5, 7}};
        for (int k = 0; k < 400; ++k) {
            std::vector<int> in(static_cast<size_t>(r.uniformInt(0, 12)));
            for (auto& v : in) v = r.uniformInt(-4, 4);
            if (k % 3 == 0) std::sort(in.begin(), in.end());
            inputs.push_back(in);
        }
        CHECK(sameBehaviour(*imp.program, *m, inputs, vars));
    }
}
