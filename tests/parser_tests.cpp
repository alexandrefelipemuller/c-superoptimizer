#include "core/Random.hpp"
#include "language/Parser.hpp"
#include "language/PrettyPrinter.hpp"
#include "search/ProgramGenerator.hpp"
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
