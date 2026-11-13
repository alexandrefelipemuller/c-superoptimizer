#pragma once

#include <string>
#include <vector>

#include "superopt/CFrontend.hpp"

namespace algosearch {

// Times C candidates against the original C function, compiled (phase 3b,
// docs/superopt.md): one executable holds both functions, renamed, in one
// translation unit with the timing loops, so gcc may inline each as a caller
// would; the two loops run interleaved in one process (log 188b: separate
// executables carried a process-level bias of up to 1.5x). Requires gcc.
class CompiledTimer {
public:
    // `originalSource`: the user's C file; `workload`: the inputs (scalar
    // arguments, or one array per input) every call is timed on.
    CompiledTimer(std::string originalSource, CFunctionAbi abi, std::vector<std::vector<int>> workload,
                  std::string workDir);

    struct Timing {
        bool ok = false;           // compiled and ran
        double referenceNs = 0;    // per call, median over trials
        double candidateNs = 0;
        double speedup = 0;        // median over trials of referenceNs / candidateNs
        std::string error;
    };

    // `candidateSource`: a C function with the same name and signature (an
    // emitCFunction output, or the original itself for a self-control).
    Timing time(const std::string& candidateSource, int trials = 11) const;

    int compilations() const { return compilations_; }

private:
    std::string originalSource_;
    CFunctionAbi abi_;
    std::string workDir_;
    std::string inputsPath_;
    mutable int compilations_ = 0;
};

}  // namespace algosearch
