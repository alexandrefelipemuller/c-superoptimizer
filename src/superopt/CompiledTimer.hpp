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
    // `byteType` (log 201): "" times each call on an argument read from an int
    // array; "unsigned char" / "signed char" (unary functions whose inputs are
    // all bytes) read it from a byte buffer instead, as a caller scanning a
    // `char *` does. gcc can compile the same function differently in the two
    // contexts (log 200: 1.41x against 1.00x).
    CompiledTimer(std::string originalSource, CFunctionAbi abi, std::vector<std::vector<int>> workload,
                  std::string workDir, std::string byteType = "");

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

    // Unary scalar functions only (log 192): the candidate against the
    // original, compiled, on all 2^32 arguments. `complete` is false when it
    // did not finish within `timeoutSeconds` (nothing proven then).
    struct Exhaustive {
        bool complete = false;
        long long mismatches = 0;
        int firstBad = 0;
        std::vector<int> bad;  // the first maxListed mismatching arguments, in order
        std::string error;
    };
    static constexpr int maxListed = 4096;
    Exhaustive exhaustive(const std::string& candidateSource, int timeoutSeconds = 60) const;

    int compilations() const { return compilations_; }

private:
    std::string originalSource_;
    CFunctionAbi abi_;
    std::string workDir_;
    std::string inputsPath_;
    std::string byteType_;
    mutable int compilations_ = 0;
};

}  // namespace algosearch
