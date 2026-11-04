// Section 157: forward reachability of sorters. Generates fresh candidates (sorting config) of one size and keeps the
// ones that sort every array of length 2..5 over values {0,1,2} (duplicates included). Prints them, "programa:" format.
// usage: sort_census SIZE COUNT SEED NUMVARS AUX(0/1) REC(0/1) [OUTER_REPEAT_BIAS] [ADJACENT_NESTED_BIAS]
// Build: g++ -std=c++20 -O2 -Isrc scripts/sort_census.cpp $(find build/lib -name '*.o' ! -name main.o) -pthread
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>

#include "interpreter/Interpreter.hpp"
#include "language/PrettyPrinter.hpp"
#include "search/ProgramGenerator.hpp"

using namespace algosearch;

int main(int argc, char** argv) {
    int size = std::atoi(argv[1]);
    long count = std::atol(argv[2]);
    unsigned long seed = std::strtoul(argv[3], nullptr, 10);
    int nv = std::atoi(argv[4]);
    bool aux = std::atoi(argv[5]), rec = std::atoi(argv[6]);
    double outerRepeat = argc > 7 ? std::atof(argv[7]) : 0.0;
    double adjNested = argc > 8 ? std::atof(argv[8]) : 0.0;
    GenConfig g;
    g.numVars = nv;
    g.loopAccumulatorBias = 0.4;
    g.localCondMutationRate = 0.3;
    g.combinatorBias = 0.5;
    g.combinatorAdjacent = g.combinatorState = g.combinatorNested = g.combinatorFrequency = true;
    g.combinatorFollow = g.followPointerInputsOnly = g.combinatorOrthogonal = g.combinatorArgBest = true;
    g.combinatorDistinct = g.followExtended = g.scalarLoop = g.combinatorTriple = g.combinatorRelax = true;
    g.combinatorGrid = g.combinatorTransform = g.distinctAggregate = g.argSentinel = true;
    g.scalarLoopIndexRange = g.combinatorStep = true;
    if (aux) g.secondArrayBias = 0.3;
    if (rec) g.recursionCallBias = 0.3;
    g.outerRepeatBias = outerRepeat;
    g.adjacentNestedBias = adjNested;
    std::vector<std::vector<int>> inputs;
    for (int n = 2; n <= 5; ++n) {
        std::vector<int> a(n, 0);
        long total = 1;
        for (int i = 0; i < n; ++i) total *= 3;
        for (long c = 0; c < total; ++c) {
            long t = c;
            for (int i = 0; i < n; ++i) { a[i] = t % 3; t /= 3; }
            inputs.push_back(a);
        }
    }
    Interpreter interp(20000, 60, rec);
    Random rng(seed);
    std::set<std::string> seen;
    long ok = 0;
    for (long k = 0; k < count; ++k) {
        auto p = generateRandomProgram(rng, size, g);
        bool good = true;
        for (const auto& in : inputs) {
            std::vector<int> out;
            if (interp.run(*p, in, nv, out) != RunStatus::COMPLETED) { good = false; break; }
            auto s = in; std::sort(s.begin(), s.end());
            if (out != s) { good = false; break; }
        }
        if (!good) continue;
        ++ok;
        std::string text = printStmt(*p);
        if (seen.insert(text).second) std::cout << "programa:\n" << text << "\n\n";
    }
    std::cerr << "size " << size << ": " << ok << " sorters in " << count << " (" << seen.size() << " distinct)\n";
}
