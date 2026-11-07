#include "core/Random.hpp"
#include "language/Parser.hpp"
#include "language/PrettyPrinter.hpp"
#include "interpreter/Interpreter.hpp"
#include "search/ProgramGenerator.hpp"
#include <algorithm>
#include <iostream>
#include "test_framework.hpp"

using namespace algosearch;

TEST(parser_round_trips_generated_programs_with_calls_and_aux) {
    GenConfig cfg;
    cfg.recursionCallBias = 0.2;
    cfg.secondArrayBias = 0.1;
    cfg.rangeShellBias = 0.5;
    Random rng(7);
    for (int i = 0; i < 300; ++i) {
        Program p = generateRandomProgram(rng, 20 + (i % 60), cfg);
        std::string text = printStmt(*p);
        Program q = parseProgram(text);
        CHECK(q != nullptr);
        if (q) CHECK_EQ(printStmt(*q), text);
    }
}

TEST(parser_rejects_garbage) {
    CHECK(parseProgram("v1 = ;") == nullptr);
    CHECK(parseProgram("while (v1 < 2) { v1 = 3;") == nullptr);
}

// Section 170: the two new sorting moves can emit a real selection sort and a real merge sort.
namespace {
bool sortsAllSmallArrays(const Stmt& program, int numVars) {
    Interpreter interp(200000, 200, true);
    for (int n = 1; n <= 4; ++n) {
        std::vector<int> a(static_cast<size_t>(n));
        int total = 1;
        for (int i = 0; i < n; ++i) total *= 3;
        for (int code = 0; code < total; ++code) {
            int c = code;
            for (int i = 0; i < n; ++i) { a[static_cast<size_t>(i)] = c % 3 - 1; c /= 3; }
            std::vector<int> out, want = a;
            std::sort(want.begin(), want.end());
            if (interp.run(program, a, numVars, out) != RunStatus::COMPLETED || out != want) return false;
        }
    }
    return true;
}
}  // namespace

TEST(selection_block_and_merge_shell_can_emit_correct_sorters) {
    GenConfig cfg;
    cfg.numVars = 8;
    cfg.recursionCallBias = 0.1;
    cfg.secondArrayBias = 0.05;
    int selection = 0, merge = 0;
    int minMerge = 1000;
    Random rng(11);
    cfg.selectionBias = 0.5;
    for (int i = 0; i < 400; ++i) {
        Program p = generateRandomProgram(rng, 35, cfg);
        if (sortsAllSmallArrays(*p, cfg.numVars)) selection++;
    }
    cfg.selectionBias = 0.0;
    cfg.mergeShellBias = 0.5;
    for (int i = 0; i < 400; ++i) {
        Program p = generateRandomProgram(rng, 80, cfg);
        if (sortsAllSmallArrays(*p, cfg.numVars)) { merge++; minMerge = std::min(minMerge, p->size()); }
    }
    CHECK(selection > 0);
    CHECK(merge > 0);
    std::cerr << "selection sorters " << selection << " merge sorters " << merge << " min merge size " << minMerge << "\n";
}
