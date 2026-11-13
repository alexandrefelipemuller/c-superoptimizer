#include <climits>
#include <string>
#include <vector>

#include "interpreter/Interpreter.hpp"
#include "language/Parser.hpp"
#include "problems/ReferenceProblem.hpp"
#include "search/Benchmark.hpp"
#include "superopt/CFrontend.hpp"
#include "test_framework.hpp"

using namespace algosearch;

namespace {

ReferenceProblem fromSource(const std::string& src) { return ReferenceProblem(importCFunction(src), 4, 7); }

// Whether `program` solves every instance of `levels`, run the way the
// search runs it (the problem decides the arithmetic semantics).
bool solvesLevels(const ReferenceProblem& p, const Stmt& program, const std::vector<int>& levels) {
    Interpreter interp(100'000);
    interp.setWrapArithmetic(p.wrapArithmetic());
    for (int level : levels)
        for (const Instance& inst : p.generateInstances(level))
            if (p.evaluate(program, inst, interp).status != ExecStatus::SUCCESS) return false;
    return true;
}

}  // namespace

TEST(reference_problem_scalar_levels_widen_and_expect_the_reference_result) {
    ReferenceProblem p = fromSource("int f(int x) { return x & (x - 1); }");
    CHECK(p.wrapArithmetic());
    std::vector<Instance> l1 = p.generateInstances(1);
    CHECK_EQ(static_cast<int>(l1.size()), 7);  // the whole grid [-3, 3]
    for (const Instance& i : l1) {
        CHECK_EQ(static_cast<int>(i.expectedOutput.size()), 1);
        CHECK_EQ(i.expectedOutput[0], i.input[0] & (i.input[0] - 1));
    }
    CHECK_EQ(ReferenceProblem::magnitude(7), 255);
    CHECK_EQ(ReferenceProblem::magnitude(30), INT_MAX);
    bool sawIntMin = false;
    for (const Instance& i : p.generateInstances(30)) sawIntMin = sawIntMin || i.input[0] == INT_MIN;
    CHECK(sawIntMin);
}

TEST(reference_problem_inputs_outside_the_domain_are_not_instances) {
    ReferenceProblem p = fromSource("int f(int x) { return 100 / x; }");
    for (int level : {1, 2, 5})
        for (const Instance& i : p.generateInstances(level)) CHECK(i.input[0] != 0);
    CHECK(!p.expected({0}).has_value());
    CHECK(p.isSolution({0}, {12345}));  // undefined in C: any output is accepted
    CHECK(!p.isSolution({4}, {24}));
    CHECK(p.isSolution({4}, {25}));
}

TEST(reference_problem_reference_solves_itself_and_a_wrong_program_does_not) {
    ReferenceProblem p = fromSource("int f(int x) { if (x < 0) return -x; return x; }");
    std::vector<int> levels = {1, 2, 3, 8, 30};
    CHECK(solvesLevels(p, p.referenceProgram(), levels));
    // abs with an off-by-one guard: right on most inputs, wrong on x = -1.
    Program wrong = parseProgram("v1 = arr[0];\nif ((v1 < -1)) {\n  v1 = (0 - v1);\n}\narr[0] = v1;\n");
    CHECK(wrong != nullptr);
    CHECK(!solvesLevels(p, *wrong, levels));
    std::string reason;
    CHECK(verifyGeneralization(p, p.referenceProgram(), 100'000, reason, false));
    CHECK(!verifyGeneralization(p, *wrong, 100'000, reason, false));
}

TEST(reference_problem_runs_candidates_with_wraparound_multiply) {
    ReferenceProblem p = fromSource("int f(int x) { return x * 65537; }");
    // The reference itself overflows on the wide levels: it must count as a
    // solution, which needs the search's interpreter in wraparound mode.
    CHECK(solvesLevels(p, p.referenceProgram(), {20, 30}));
    Interpreter strict(100'000);
    bool anyError = false;
    for (const Instance& inst : p.generateInstances(30))
        anyError = anyError || p.evaluate(p.referenceProgram(), inst, strict).status == ExecStatus::RUNTIME_ERROR;
    CHECK(anyError);
}

TEST(reference_problem_array_levels_are_lengths_and_void_results_are_arrays) {
    ReferenceProblem p = fromSource(
        "void f(int *a, int n) { int i = 0, j = n - 1; while (i < j) { int t = a[i]; a[i] = a[j]; a[j] = t; i++; j--; } }");
    bool sawEmpty = false;
    for (int n : {1, 3, 6})
        for (const Instance& i : p.generateInstances(n)) {
            sawEmpty = sawEmpty || i.input.empty();
            if (i.input.empty()) CHECK_EQ(n, 1);  // level 1 also tests n = 0
            else CHECK_EQ(static_cast<int>(i.input.size()), n);
            std::vector<int> rev(i.input.rbegin(), i.input.rend());
            CHECK(i.expectedOutput == rev);
        }
    CHECK(sawEmpty);
    CHECK(solvesLevels(p, p.referenceProgram(), {1, 2, 5, 9}));
    // Half the positions right earns partial credit, a correct array full credit.
    CHECK(p.partialScore({1, 2, 3, 4}, {4, 3, 0, 0}) > 0.4);
    CHECK(p.partialScore({1, 2, 3, 4}, {4, 3, 0, 0}) < 1.0);
    CHECK(p.partialScore({1, 2, 3, 4}, {4, 3, 2, 1}) == 1.0);
}

TEST(reference_problem_exhaustive_inputs_cover_small_domains) {
    ReferenceProblem scalar = fromSource("int f(int a, int b) { return a > b ? a : b; }");
    CHECK_EQ(static_cast<int>(scalar.exhaustiveInputs(1).size()), 129 * 129);
    CHECK(scalar.exhaustiveInputs(2).empty());
    ReferenceProblem array = fromSource("int f(int *a, int n) { int s = 0; for (int i = 0; i < n; i++) s += a[i]; return s; }");
    CHECK_EQ(static_cast<int>(array.exhaustiveInputs(3).size()), 125);
    CHECK(array.exhaustiveInputs(6).empty());
}

TEST(reference_problem_level_one_catches_a_loop_that_reads_an_empty_array) {
    // Log 188: superopt changed `v1 < v0` into `v1 - v0`; equal on n >= 1,
    // reads arr[1] when n = 0 (a segfault in the compiled C).
    ReferenceProblem p = fromSource(
        "void isort(int *a, int n) { for (int i = 1; i < n; i++) { int k = a[i]; int j = i - 1;"
        " while (j >= 0 && a[j] > k) { a[j + 1] = a[j]; j--; } a[j + 1] = k; } }");
    Program bad = parseProgram(
        "v1 = 1;\nwhile ((v1 - v0)) {\n  v3 = (v1 - 1);\n  v2 = arr[v1];\n  v1 = (v1 + 1);\n  while ((v3 + 1)) {\n"
        "    v4 = (arr[v3] > v2);\n    if ((v4 == 0)) {\n      break;\n    }\n    arr[(v3 + 1)] = arr[v3];\n"
        "    v3 = (v3 - v4);\n  }\n  arr[(v3 + 1)] = v2;\n}\n");
    CHECK(bad != nullptr);
    CHECK(solvesLevels(p, *bad, {2, 3, 5, 9}));  // right whenever n >= 2
    CHECK(!solvesLevels(p, *bad, {1}));          // the empty array exposes it
}
