#include "superopt/CompiledTimer.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>

namespace algosearch {

namespace {

// Timing translation unit. %REF% / %CAND% are the two function sources,
// renamed ref_fn / cand_fn; %CALL_REF% / %CALL_CAND% one call on input i.
const char* harnessTemplate = R"(#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
%REF%
%CAND%
static volatile long long h_sink;
static int h_nin, h_len[65536], h_off[65536], h_data[4000000], h_buf[4096];
static double h_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e9 + t.tv_nsec; }
__attribute__((noinline, aligned(64))) static long long run_ref(int reps) {
    long long acc = 0;
    for (int r = 0; r < reps; r++)
        for (int i = 0; i < h_nin; i++) { %CALL_REF% }
    return acc;
}
__attribute__((noinline, aligned(64))) static long long run_cand(int reps) {
    long long acc = 0;
    for (int r = 0; r < reps; r++)
        for (int i = 0; i < h_nin; i++) { %CALL_CAND% }
    return acc;
}
static int h_cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
int main(int argc, char **argv) {
    int trials = atoi(argv[2]);
    FILE *f = fopen(argv[1], "r");
    char line[65536];
    int pos = 0;
    while (fgets(line, sizeof line, f)) {
        char *p = line, *end;
        h_off[h_nin] = pos; h_len[h_nin] = 0;
        for (;;) { long x = strtol(p, &end, 10); if (end == p) break; h_data[pos++] = (int)x; h_len[h_nin]++; p = end; }
        h_nin++;
    }
    fclose(f);
    int reps = 1;
    for (;;) { double t0 = h_now(); h_sink += run_ref(reps); if (h_now() - t0 > 4e6 || reps > (1 << 22)) break; reps *= 2; }
    h_sink += run_cand(reps);
    double tr[64], tc[64], ratio[64];
    for (int k = 0; k < trials; k++) {
        double t0, t1, t2;
        if (k & 1) { t0 = h_now(); h_sink += run_cand(reps); t1 = h_now(); h_sink += run_ref(reps); t2 = h_now(); tc[k] = t1 - t0; tr[k] = t2 - t1; }
        else { t0 = h_now(); h_sink += run_ref(reps); t1 = h_now(); h_sink += run_cand(reps); t2 = h_now(); tr[k] = t1 - t0; tc[k] = t2 - t1; }
        ratio[k] = tr[k] / tc[k];
        tr[k] /= (double)reps * h_nin;
        tc[k] /= (double)reps * h_nin;
    }
    qsort(tr, trials, sizeof(double), h_cmp);
    qsort(tc, trials, sizeof(double), h_cmp);
    qsort(ratio, trials, sizeof(double), h_cmp);
    printf("%f %f %f\n", tr[trials / 2], tc[trials / 2], ratio[trials / 2]);
    return 0;
}
)";

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    for (size_t p = s.find(from); p != std::string::npos; p = s.find(from, p + to.size())) s.replace(p, from.size(), to);
    return s;
}

// Every function the source defines gets the side's prefix (helpers too, so
// an original with helpers can be timed against itself in one unit); the
// timed function becomes `<prefix>fn`.
std::string prefixFunctions(const std::string& src, const std::string& prefix, const std::string& target) {
    static const std::regex def("\\b(?:int|void|int32_t)\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\(");
    std::string out = src;
    for (auto it = std::sregex_iterator(src.begin(), src.end(), def); it != std::sregex_iterator(); ++it) {
        std::string name = (*it)[1].str();
        out = std::regex_replace(out, std::regex("\\b" + name + "\\b"), prefix + (name == target ? "fn" : name));
    }
    return out;
}

std::string callFor(const CFunctionAbi& abi, const std::string& fn) {
    if (abi.kind == CFunctionAbi::Kind::Scalars) {
        std::string args;
        for (size_t k = 0; k < abi.params.size(); ++k)
            args += (k ? ", " : "") + std::string("h_data[h_off[i] + ") + std::to_string(k) + "]";
        return "acc += " + fn + "(" + args + ");";
    }
    std::string copy = "memcpy(h_buf, h_data + h_off[i], sizeof(int) * h_len[i]); ";
    if (abi.returnsValue) return copy + "acc += " + fn + "(h_buf, h_len[i]);";
    return copy + fn + "(h_buf, h_len[i]); acc += h_buf[0];";
}

}  // namespace

CompiledTimer::CompiledTimer(std::string originalSource, CFunctionAbi abi, std::vector<std::vector<int>> workload,
                             std::string workDir)
    : originalSource_(std::move(originalSource)), abi_(std::move(abi)), workDir_(std::move(workDir)) {
    if (std::system(("mkdir -p '" + workDir_ + "'").c_str()) != 0)
        throw CImportError("cannot create the timing directory " + workDir_);
    inputsPath_ = workDir_ + "/inputs.txt";
    std::ofstream in(inputsPath_);
    for (const auto& w : workload) {
        for (size_t k = 0; k < w.size(); ++k) in << (k ? " " : "") << w[k];
        in << "\n";
    }
}

CompiledTimer::Timing CompiledTimer::time(const std::string& candidateSource, int trials) const {
    Timing t;
    std::string src = harnessTemplate;
    src = replaceAll(src, "%REF%", prefixFunctions(originalSource_, "ref_", abi_.name));
    src = replaceAll(src, "%CAND%", prefixFunctions(candidateSource, "cand_", abi_.name));
    src = replaceAll(src, "%CALL_REF%", callFor(abi_, "ref_fn"));
    src = replaceAll(src, "%CALL_CAND%", callFor(abi_, "cand_fn"));
    std::string base = workDir_ + "/t" + std::to_string(compilations_++);
    {
        std::ofstream(base + ".c") << src;
    }
    std::string cc = "gcc -O2 -fwrapv -w -falign-functions=64 -falign-loops=64 '" + base + ".c' -o '" + base +
                     "' 2> '" + base + ".err'";
    if (std::system(cc.c_str()) != 0) {
        std::ifstream err(base + ".err");
        std::stringstream ss;
        ss << err.rdbuf();
        t.error = "gcc failed: " + ss.str().substr(0, 300);
        return t;
    }
    std::string cmd = "'" + base + "' '" + inputsPath_ + "' " + std::to_string(std::min(trials, 63));
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) {
        t.error = "cannot run " + base;
        return t;
    }
    if (std::fscanf(p, "%lf %lf %lf", &t.referenceNs, &t.candidateNs, &t.speedup) == 3) t.ok = true;
    int rc = pclose(p);
    if (rc != 0) {
        t.ok = false;
        t.error = "timing run failed (exit " + std::to_string(rc) + ")";
    }
    std::remove((base + ".c").c_str());
    std::remove(base.c_str());
    std::remove((base + ".err").c_str());
    return t;
}

CompiledTimer::Exhaustive CompiledTimer::exhaustive(const std::string& candidateSource, int timeoutSeconds) const {
    Exhaustive x;
    if (abi_.kind != CFunctionAbi::Kind::Scalars || abi_.params.size() != 1) {
        x.error = "exhaustive check needs a unary scalar function";
        return x;
    }
    std::string src = "#include <stdio.h>\n" + prefixFunctions(originalSource_, "ref_", abi_.name) + "\n" +
                      prefixFunctions(candidateSource, "cand_", abi_.name) +
                      "\nint main(void) {\n"
                      "    long long bad = 0; int first = 0;\n"
                      "    for (long long y = -2147483648LL; y <= 2147483647LL; y++)\n"
                      "        if (ref_fn((int)y) != cand_fn((int)y)) { if (!bad) first = (int)y; bad++; }\n"
                      "    printf(\"%lld %d\\n\", bad, first);\n"
                      "    return 0;\n}\n";
    std::string base = workDir_ + "/x" + std::to_string(compilations_++);
    {
        std::ofstream(base + ".c") << src;
    }
    std::string cc = "gcc -O2 -fwrapv -w '" + base + ".c' -o '" + base + "' 2> '" + base + ".err'";
    if (std::system(cc.c_str()) != 0) {
        x.error = "gcc failed";
        return x;
    }
    std::string cmd = "timeout " + std::to_string(timeoutSeconds) + " '" + base + "'";
    FILE* p = popen(cmd.c_str(), "r");
    if (p) {
        if (std::fscanf(p, "%lld %d", &x.mismatches, &x.firstBad) == 2) x.complete = true;
        if (pclose(p) != 0) x.complete = false;
    }
    if (!x.complete) x.error = "did not finish in " + std::to_string(timeoutSeconds) + " s";
    std::remove((base + ".c").c_str());
    std::remove(base.c_str());
    std::remove((base + ".err").c_str());
    return x;
}

}  // namespace algosearch
