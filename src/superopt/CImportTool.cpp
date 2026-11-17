#include "superopt/CImportTool.hpp"

#include <fstream>
#include <unistd.h>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "interpreter/Interpreter.hpp"
#include "language/Parser.hpp"
#include "language/PrettyPrinter.hpp"
#include "problems/ReferenceProblem.hpp"
#include "superopt/CFrontend.hpp"
#include "superopt/Superoptimizer.hpp"

namespace algosearch {

namespace {

// A result or parameter narrower than 32 bits (log 197): scripts that call
// the function from another unit as `int f(int, ...)` need a typed wrapper.
bool narrowSignature(const CFunctionAbi& abi) {
    auto narrow = [](const std::string& t) { return t != "int" && t != "unsigned"; };
    if (abi.returnsValue && narrow(abi.returnType)) return true;
    for (const auto& t : abi.paramTypes)
        if (narrow(t)) return true;
    return false;
}

}  // namespace


namespace {

bool readFile(const std::string& path, std::string& out) {
    std::ifstream in(path);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

const char* kindName(CFunctionAbi::Kind k) { return k == CFunctionAbi::Kind::Array ? "array" : "scalars"; }

}  // namespace

int runCImportTool(int argc, char** argv) {
    std::string file, func, evalPath, programPath;
    bool emit = false;
    long stepLimit = 10'000'000;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << a << "\n";
                std::exit(1);
            }
            return argv[++i];
        };
        if (a == "--func") func = next();
        else if (a == "--emit") emit = true;
        else if (a == "--eval") evalPath = next();
        else if (a == "--program") programPath = next();
        else if (a == "--step-limit") stepLimit = std::stol(next());
        else if (file.empty() && !a.empty() && a[0] != '-') file = a;
        else {
            std::cerr << "Unknown argument: " << a << "\n";
            file.clear();
            break;
        }
    }
    if (file.empty()) {
        std::cerr << "usage: algosearch c-import FILE.c [--func NAME] [--program MINIC] [--emit] [--eval INPUTS] [--step-limit N]\n";
        return 1;
    }
    std::string source;
    if (!readFile(file, source)) {
        std::cerr << "cannot read " << file << "\n";
        return 1;
    }
    try {
        CImport imp = importCFunction(source, func);
        if (!programPath.empty()) {
            // Another Mini-C program (e.g. a search result) under FILE's ABI.
            std::string text;
            if (!readFile(programPath, text)) {
                std::cerr << "cannot read " << programPath << "\n";
                return 1;
            }
            Program p = parseProgram(text);
            if (!p) {
                std::cerr << programPath << ": not a Mini-C program\n";
                return 1;
            }
            imp.program = std::move(p);
        }
        if (emit) {
            std::cout << emitCFunction(*imp.program, imp.abi);
            return 0;
        }
        if (!evalPath.empty()) {
            std::ifstream in(evalPath);
            if (!in) {
                std::cerr << "cannot read " << evalPath << "\n";
                return 1;
            }
            Interpreter interp(stepLimit);
            interp.setWrapArithmetic(true);
            std::string line;
            std::vector<int> output;
            while (std::getline(in, line)) {
                for (char& c : line)
                    if (c == ',') c = ' ';
                std::istringstream ls(line);
                std::vector<int> input;
                long long v;
                while (ls >> v) input.push_back(static_cast<int>(v));
                RunStatus st = interp.run(*imp.program, input, imp.abi.numVars, output);
                if (st == RunStatus::TIMEOUT) {
                    std::cout << "TIMEOUT\n";
                } else if (st == RunStatus::RUNTIME_ERROR) {
                    std::cout << "ERROR\n";
                } else {
                    std::vector<int> r = abiResult(imp.abi, output);
                    for (size_t k = 0; k < r.size(); ++k) std::cout << (k ? " " : "") << r[k];
                    std::cout << "\n";
                }
            }
            return 0;
        }
        std::cout << "// " << imp.abi.name << ": abi=" << kindName(imp.abi.kind)
                  << " returns=" << (imp.abi.returnsValue ? "int" : "void")
                  << " params=" << imp.abi.params.size() << " vars=" << imp.abi.numVars
                  << " size=" << imp.program->size() << (narrowSignature(imp.abi) ? " narrow=1" : "") << "\n";
        std::cout << printStmt(*imp.program);
        return 0;
    } catch (const CImportError& e) {
        std::cerr << file << ": " << e.what() << "\n";
        return 2;
    }
}

int runSuperoptTool(int argc, char** argv) {
    std::string file, func, outPath, workloadPath;
    SuperoptConfig cfg;
    int numVars = -1;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << a << "\n";
                std::exit(1);
            }
            return argv[++i];
        };
        if (a == "--func") func = next();
        else if (a == "--seconds") cfg.wallSeconds = std::stod(next());
        else if (a == "--beam") cfg.beamWidth = cfg.exploreWidth = std::stoi(next());
        else if (a == "--mutants") cfg.mutantsPerParent = std::stoi(next());
        else if (a == "--seed") cfg.seed = std::stoull(next());
        else if (a == "--max-n") cfg.arrayMaxN = std::stoi(next());
        else if (a == "--num-vars") numVars = std::stoi(next());
        else if (a == "--out") outPath = next();
        else if (a == "--verbose") cfg.verbose = true;
        else if (a == "--time-top") cfg.timeTopK = std::stoi(next());
        else if (a == "--algorithmic-moves") cfg.algorithmicMoves = true;
        else if (a == "--no-algorithmic-moves") cfg.algorithmicMoves = false;
        else if (a == "--work-dir") cfg.workDir = next();
        else if (a == "--workload") workloadPath = next();
        else if (a == "--cross-context-confirm") cfg.crossContextConfirm = true;
        else if (a == "--timing-context") {
            cfg.timingContext = next();
            if (cfg.timingContext != "int" && cfg.timingContext != "byte") {
                std::cerr << "--timing-context takes int or byte\n";
                return 1;
            }
        }
        else if (file.empty() && !a.empty() && a[0] != '-') file = a;
        else {
            std::cerr << "Unknown argument: " << a << "\n";
            file.clear();
            break;
        }
    }
    if (file.empty()) {
        std::cerr << "usage: algosearch superopt FILE.c [--func NAME] [--seconds 30] [--beam 40] [--mutants 12] "
                     "[--seed 1] [--max-n 10] [--num-vars N] [--out FOUND.c] [--verbose] [--time-top K] [--work-dir DIR] [--no-algorithmic-moves] [--workload INPUTS] [--timing-context int|byte] [--cross-context-confirm]\n";
        return 1;
    }
    try {
        std::string source;
        if (!readFile(file, source)) {
            std::cerr << "cannot read " << file << "\n";
            return 1;
        }
        CImport imp = importCFunction(source, func);
        if (!workloadPath.empty()) {
            // One input per line, as for `c-import --eval`.
            std::ifstream in(workloadPath);
            if (!in) {
                std::cerr << "cannot read " << workloadPath << "\n";
                return 1;
            }
            std::string line;
            while (std::getline(in, line)) {
                for (char& c : line)
                    if (c == ',') c = ' ';
                std::istringstream ls(line);
                std::vector<int> input;
                long long v;
                while (ls >> v) input.push_back(static_cast<int>(v));
                bool ok = imp.abi.kind == CFunctionAbi::Kind::Scalars ? input.size() == imp.abi.params.size()
                                                                       : !input.empty();
                if (!ok) {
                    std::cerr << workloadPath << ": an input does not match the signature: " << line << "\n";
                    return 1;
                }
                cfg.userWorkload.push_back(std::move(input));
            }
        }
        int vars = numVars > 0 ? numVars : imp.abi.numVars + 2;
        cfg.originalSource = source;
        if (cfg.workDir == SuperoptConfig().workDir) cfg.workDir += "_" + std::to_string(getpid());
        // Inputs on which the reference needs more than 1M steps are outside
        // the domain the result is checked on (docs/superopt.md).
        ReferenceProblem problem(std::move(imp), vars, cfg.seed, 1'000'000);
        Superoptimizer opt(problem, cfg);
        SuperoptResult r = opt.run();
        double ratio = r.bestCost > 0 ? static_cast<double>(r.referenceCost) / static_cast<double>(r.bestCost) : 0.0;
        std::cout << "reference_cost " << r.referenceCost << "\nbest_cost " << r.bestCost << "\nmodel_speedup " << ratio
                  << "\nreference_size " << problem.referenceProgram().size() << "\nbest_size " << r.best->size()
                  << "\nevaluated " << r.evaluated << "\ncorrect " << r.correct << "\ntests " << r.tests
                  << "\ncounterexamples " << r.counterexamples << "\nuser_workload " << r.userWorkload << "\nverified " << (r.verified ? "yes" : "NO")
                  << (r.verified ? "" : " (" + r.verifyReason + ")") << "\n";
        if (cfg.timeTopK > 0) {
            std::cout << "timings " << r.timings << "\ntiming_seconds " << r.timingSeconds << "\ntiming_context " << r.timingContext << "\ncross_context_self_control " << r.crossContextSelfControl << "\ncross_context_speedup " << r.crossContextSpeedup << "\ncross_context_rejected " << r.crossContextRejected << "\nself_control "
                      << r.selfControl << "\nconfirm_self_control " << r.confirmSelfControl << "\nnoise_threshold " << r.noiseThreshold << "\nemitted_reference "
                      << r.emittedReference << "\nmeasured_speedup " << r.measuredSpeedup << "\nequivalence_level " << r.equivalence
                      << "\nexhaustive_rejected " << r.exhaustiveRejected << "\nfuzz_rejected " << r.fuzzRejected << "\nmodel_best_cost "
                      << r.modelBestCost << "\n";
        }
        std::cout << "trajectory";
        for (const auto& [t, c] : r.trajectory) std::cout << " " << t << ":" << c;
        std::cout << "\n--- Mini-C\n" << printStmt(*r.best);
        std::string c = emitCFunction(*r.best, problem.abi());
        // Timed mode with no measurable gain: the original text itself, not
        // the reference re-emitted from Mini-C, which can compile worse (log
        // 191: sat_add's `&&` became `&`, 0.54x).
        bool original = cfg.timeTopK > 0 && printStmt(*r.best) == printStmt(problem.referenceProgram());
        if (original) c = source;
        std::cout << "returned " << (original ? "original" : "candidate") << "\n--- C\n" << c;
        if (!outPath.empty()) std::ofstream(outPath) << c;
        return r.verified ? 0 : 3;
    } catch (const CImportError& e) {
        std::cerr << file << ": " << e.what() << "\n";
        return 2;
    }
}

}  // namespace algosearch
