#include "superopt/CImportTool.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "interpreter/Interpreter.hpp"
#include "language/PrettyPrinter.hpp"
#include "superopt/CFrontend.hpp"

namespace algosearch {

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
    std::string file, func, evalPath;
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
        else if (a == "--step-limit") stepLimit = std::stol(next());
        else if (file.empty() && !a.empty() && a[0] != '-') file = a;
        else {
            std::cerr << "Unknown argument: " << a << "\n";
            file.clear();
            break;
        }
    }
    if (file.empty()) {
        std::cerr << "usage: algosearch c-import FILE.c [--func NAME] [--emit] [--eval INPUTS] [--step-limit N]\n";
        return 1;
    }
    std::string source;
    if (!readFile(file, source)) {
        std::cerr << "cannot read " << file << "\n";
        return 1;
    }
    try {
        CImport imp = importCFunction(source, func);
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
                  << " size=" << imp.program->size() << "\n";
        std::cout << printStmt(*imp.program);
        return 0;
    } catch (const CImportError& e) {
        std::cerr << file << ": " << e.what() << "\n";
        return 2;
    }
}

}  // namespace algosearch
