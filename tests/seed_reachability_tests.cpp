#include <iostream>

#include "interpreter/Interpreter.hpp"
#include "language/Parser.hpp"
#include "problems/MinGapProblem.hpp"
#include "test_framework.hpp"

using namespace algosearch;

// Section 173: a hand-written selection sort plus adjacent-minimum pass solves min_gap in the language (reachability).
TEST(selection_sort_then_adjacent_min_pass_solves_min_gap) {
    const std::string text =
    "if ((v0 > 1)) {\n"
    "  v3 = v1;\n"
    "  while ((v3 < v2)) {\n"
    "    v4 = v3;\n"
    "    v5 = (v3 + 1);\n"
    "    while ((v5 < v2)) {\n"
    "      if ((arr[v5] < arr[v4])) {\n"
    "        v4 = v5;\n"
    "      }\n"
    "      v5 = (v5 + 1);\n"
    "    }\n"
    "    swap(arr[v3], arr[v4]);\n"
    "    v3 = (v3 + 1);\n"
    "  }\n"
    "  v7 = (arr[1] - arr[0]);\n"
    "  v6 = 1;\n"
    "  while ((v6 < v0)) {\n"
    "    if (((arr[v6] - arr[(v6 - 1)]) < v7)) {\n"
    "      v7 = (arr[v6] - arr[(v6 - 1)]);\n"
    "    }\n"
    "    v6 = (v6 + 1);\n"
    "  }\n"
    "  arr[0] = v7;\n"
    "} else {\n"
    "  arr[0] = 0;\n"
    "}\n";
    Program p = parseProgram(text);
    CHECK(p != nullptr);
    if (!p) return;
    MinGapProblem problem(8, 42);
    Interpreter interp(20000, 200, true);
    int bad = 0, total = 0;
    for (int level = 1; level <= 6; ++level) {
        for (const auto& inst : problem.generateInstances(level)) {
            std::vector<int> out;
            auto st = interp.run(*p, inst.input, 8, out);
            total++;
            const bool ok = st == RunStatus::COMPLETED && !out.empty() && out[0] == inst.expectedOutput[0];
            if (!ok) {
                bad++;
                if (bad <= 5) {
                    std::cerr << "FAIL level " << level << " input";
                    for (int v : inst.input) std::cerr << " " << v;
                    std::cerr << " expected " << inst.expectedOutput[0] << " got " << (out.empty() ? -999 : out[0]) << " status " << static_cast<int>(st) << "\n";
                }
            }
        }
    }
    std::cerr << "seed program: " << bad << " wrong of " << total << ", size " << p->size() << "\n";
    CHECK_EQ(bad, 0);
}
