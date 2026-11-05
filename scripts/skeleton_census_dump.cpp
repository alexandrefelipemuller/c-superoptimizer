// Section 156: dump fresh candidates exactly as the search generates them (default CLI GenConfig, one size per
// call), deduplicated by text, one program per block ("programa:" format), for scripts/skeleton_census.py.
// Build: g++ -std=c++20 -O2 -Isrc scripts/skeleton_census_dump.cpp $(find build/lib -name '*.o' ! -name main.o) -pthread
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>

#include "language/PrettyPrinter.hpp"
#include "search/ProgramGenerator.hpp"

using namespace algosearch;

int main(int argc, char** argv) {
    int size = std::atoi(argv[1]);
    int count = std::atoi(argv[2]);
    unsigned long seed = std::strtoul(argv[3], nullptr, 10);
    GenConfig g;  // defaults of main.cpp's Args
    g.numVars = argc > 4 ? std::atoi(argv[4]) : 4;
    g.loopAccumulatorBias = 0.4;
    g.localCondMutationRate = 0.3;
    g.combinatorBias = 0.5;
    g.combinatorAdjacent = g.combinatorState = g.combinatorNested = g.combinatorFrequency = true;
    g.combinatorFollow = g.followPointerInputsOnly = g.combinatorOrthogonal = g.combinatorArgBest = true;
    g.combinatorDistinct = g.followExtended = g.scalarLoop = g.combinatorTriple = g.combinatorRelax = true;
    g.combinatorGrid = g.combinatorTransform = g.distinctAggregate = g.argSentinel = true;
    g.scalarLoopIndexRange = g.combinatorStep = true;
    g.termExprBias = 0.3;
    g.prefixCrossBias = 0.3;
    g.parityTermBias = 0.03;
    g.pairFindBias = 0.05;
    g.sumIfBias = 0.05;
    g.radixBias = 0.03;
    g.digitLoopBias = 0.015;
    g.counterModBias = 0.03;
    g.signedParityBias = 0.015;
    g.countModBias = 0.1;
    if (argc > 5) g.recursionCallBias = std::atof(argv[5]);
    if (argc > 6) g.secondArrayBias = std::atof(argv[6]);
    Random rng(seed);
    std::set<std::string> seen;
    int guard = 0;
    while (static_cast<int>(seen.size()) < count && guard < count * 4) {
        auto p = generateRandomProgram(rng, size, g);
        std::string text = printStmt(*p);
        ++guard;
        if (seen.insert(text).second) std::cout << "programa:\n" << text << "\n\n";
    }
}
