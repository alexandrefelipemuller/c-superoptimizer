#include "superopt/CompiledTimer.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <set>
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
static unsigned char h_u8[65536];
static signed char h_s8[65536];
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
static long long h_check(int i, int side) {
    long long acc = 0;
    if (side) { %CALL_CAND% } else { %CALL_REF% }
    return acc;
}
static int h_cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
int main(int argc, char **argv) {
    int trials = atoi(argv[2]);
    FILE *f = fopen(argv[1], "r");
    char line[65536];
    int pos = 0;
    while (h_nin < 65536 && pos + 4096 <= 4000000 && fgets(line, sizeof line, f)) {
        char *p = line, *end;
        h_off[h_nin] = pos; h_len[h_nin] = 0;
        for (;;) { long x = strtol(p, &end, 10); if (end == p) break; h_data[pos++] = (int)x; h_len[h_nin]++; p = end; }
        h_u8[h_nin] = (unsigned char)h_data[h_off[h_nin]]; h_s8[h_nin] = (signed char)h_data[h_off[h_nin]];
        h_nin++;
    }
    fclose(f);
    if (argc > 3) {  /* log 210: differential check, no timing */
        long long bad = 0; int first = -1;
        for (int i = 0; i < h_nin; i++)
            if (h_check(i, 0) != h_check(i, 1)) { bad++; if (first < 0) first = i; }
        printf("%lld %d %d\n", bad, first, h_nin);
        return 0;
    }
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
    static const std::regex def("\\b(?:int|void|int32_t|unsigned|uint32_t|char|short|uint8_t|int8_t|uint16_t|int16_t|"
                                "long|size_t|ssize_t|uint64_t|int64_t|ptrdiff_t|uintptr_t|intptr_t)\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\(");
    // Log 210: also definitions whose return type is a typedef name
    // (`static uLong f(`, `psa_status_t g(`), at the start of a line.
    static const std::regex typedefDef("(?:^|\\n)[ \\t]*(?:static[ \\t]+|inline[ \\t]+)*(?:const[ \\t]+)?([A-Za-z_][A-Za-z0-9_]*)"
                                       "[ \\t\\*]+([A-Za-z_][A-Za-z0-9_]*)[ \\t]*\\(");
    static const std::set<std::string> notTypes = {"return", "typedef", "else", "if", "while", "for", "do", "switch",
                                                   "case", "goto", "sizeof", "struct", "union", "enum"};
    std::set<std::string> names = {target};
    for (auto it = std::sregex_iterator(src.begin(), src.end(), def); it != std::sregex_iterator(); ++it)
        names.insert((*it)[1].str());
    for (auto it = std::sregex_iterator(src.begin(), src.end(), typedefDef); it != std::sregex_iterator(); ++it)
        if (!notTypes.count((*it)[1].str()) && !notTypes.count((*it)[2].str())) names.insert((*it)[2].str());
    std::string out = src;
    for (const auto& name : names)
        out = std::regex_replace(out, std::regex("\\b" + name + "\\b"), prefix + (name == target ? "fn" : name));
    return out;
}

std::string callFor(const CFunctionAbi& abi, const std::string& fn, const std::string& byteType) {
    if (byteType == "unsigned char") return "acc += " + fn + "(h_u8[i]);";
    if (byteType == "signed char") return "acc += " + fn + "(h_s8[i]);";
    if (abi.kind == CFunctionAbi::Kind::Scalars) {
        std::string args;
        for (size_t k = 0; k < abi.params.size(); ++k)
            args += (k ? ", " : "") + std::string("h_data[h_off[i] + ") + std::to_string(k) + "]";
        return "acc += " + fn + "(" + args + ");";
    }
    std::string target = needsAdapter(abi) ? fn + "_abi" : fn;
    std::string copy = "memcpy(h_buf, h_data + h_off[i], sizeof(int) * h_len[i]); ";
    if (needsAdapter(abi)) {
        if (abi.returnsValue) return copy + "acc += " + target + "(h_buf, h_len[i]);";
        return copy + target + "(h_buf, h_len[i]); acc += h_buf[0];";
    }
    if (abi.returnsValue) return copy + "acc += " + fn + "(h_buf, h_len[i]);";
    return copy + fn + "(h_buf, h_len[i]); acc += h_buf[0];";
}

}  // namespace

CompiledTimer::CompiledTimer(std::string originalSource, CFunctionAbi abi, std::vector<std::vector<int>> workload,
                             std::string workDir, std::string byteType, std::string programTimer)
    : originalSource_(std::move(originalSource)), abi_(std::move(abi)), workDir_(std::move(workDir)),
      byteType_(std::move(byteType)), programTimer_(std::move(programTimer)) {
    if (!byteType_.empty() && (abi_.kind != CFunctionAbi::Kind::Scalars || abi_.params.size() != 1))
        throw CImportError("byte timing context needs a unary scalar function");
    if (std::system(("mkdir -p '" + workDir_ + "'").c_str()) != 0)
        throw CImportError("cannot create the timing directory " + workDir_);
    inputsPath_ = workDir_ + "/inputs.txt";
    std::ofstream in(inputsPath_);
    for (const auto& w : workload) {
        for (size_t k = 0; k < w.size(); ++k) in << (k ? " " : "") << w[k];
        in << "\n";
    }
}

CompiledTimer::Check CompiledTimer::check(const std::string& candidateSource) const {
    Check c;
    std::string base = workDir_ + "/c" + std::to_string(compilations_++);
    {
        std::ofstream(base + ".c") << harnessSource(candidateSource);
    }
    std::string cc = "gcc -O2 -fwrapv -w '" + base + ".c' -o '" + base + "' 2> '" + base + ".err'";
    if (std::system(cc.c_str()) != 0) {
        std::ifstream err(base + ".err");
        std::stringstream ss;
        ss << err.rdbuf();
        c.error = "gcc failed: " + ss.str().substr(0, 300);
        return c;
    }
    std::string cmd = "timeout 120 '" + base + "' '" + inputsPath_ + "' 1 check";
    FILE* p = popen(cmd.c_str(), "r");
    if (p) {
        if (std::fscanf(p, "%lld %d %d", &c.mismatches, &c.firstBad, &c.inputs) == 3) c.ok = true;
        if (pclose(p) != 0) c.ok = false;
    }
    if (!c.ok) c.error = "check run failed";
    std::remove((base + ".c").c_str());
    std::remove(base.c_str());
    return c;
}

std::string CompiledTimer::harnessSource(const std::string& candidateSource) const {
    std::string src = harnessTemplate;
    std::string ref = prefixFunctions(originalSource_, "ref_", abi_.name);
    std::string cand = prefixFunctions(candidateSource, "cand_", abi_.name);
    // Log 210: the candidate may be the original itself (self-control), so
    // every type name the original declares (struct tags, typedefs: a unit
    // cut from a real translation unit carries many) is renamed on the
    // candidate's side by macros, as the adapter ABIs already did.
    std::set<std::string> typeNames(abi_.structNames.begin(), abi_.structNames.end());
    static const std::regex tagDef("\\b(?:struct|union|enum)\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\{");
    static const std::regex typedefWord("\\btypedef\\b");
    static const std::regex fnPtrName("\\(\\s*\\*\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*\\)");
    static const std::regex lastIdent("([A-Za-z_][A-Za-z0-9_]*)\\s*(?:\\[[^\\]]*\\]\\s*)*$");
    for (auto it = std::sregex_iterator(originalSource_.begin(), originalSource_.end(), tagDef); it != std::sregex_iterator(); ++it)
        typeNames.insert((*it)[1].str());
    // Each typedef up to its `;` at brace depth 0, the braces' contents dropped
    // (`typedef struct { ...; } T;`).
    for (auto it = std::sregex_iterator(originalSource_.begin(), originalSource_.end(), typedefWord); it != std::sregex_iterator(); ++it) {
        std::string body;
        int depth = 0;
        for (size_t k = static_cast<size_t>(it->position() + it->length()); k < originalSource_.size(); ++k) {
            char ch = originalSource_[k];
            if (ch == '{') ++depth;
            if (ch == '}') --depth;
            if (ch == ';' && depth == 0) break;
            if (depth == 0 && ch != '}') body += ch;
        }
        std::smatch m;
        if (std::regex_search(body, m, fnPtrName) || std::regex_search(body, m, lastIdent)) typeNames.insert(m[1].str());
    }
    std::string on, off;
    for (const auto& n : typeNames) {
        on += "#define " + n + " cand_struct_" + n + "\n";
        off += "#undef " + n + "\n";
    }
    if (needsAdapter(abi_)) {
        ref += "\n" + abiAdapter(abi_, "ref_fn_abi", "ref_fn");
        cand = on + cand + "\n" + abiAdapter(abi_, "cand_fn_abi", "cand_fn") + off;
    } else {
        cand = on + cand + "\n" + off;
    }
    src = replaceAll(src, "%REF%", ref);
    src = replaceAll(src, "%CAND%", cand);
    src = replaceAll(src, "%CALL_REF%", callFor(abi_, "ref_fn", byteType_));
    src = replaceAll(src, "%CALL_CAND%", callFor(abi_, "cand_fn", byteType_));
    return src;
}

CompiledTimer::Timing CompiledTimer::time(const std::string& candidateSource, int trials) const {
    Timing t;
    if (!programTimer_.empty()) {
        std::string path = workDir_ + "/p" + std::to_string(compilations_++) + ".c";
        {
            std::ofstream(path) << candidateSource;
        }
        std::string cmd = programTimer_ + " '" + path + "' " + std::to_string(trials) + " 2> '" + path + ".err'";
        FILE* p = popen(cmd.c_str(), "r");
        if (!p) {
            t.error = "cannot run the program timer";
            return t;
        }
        if (std::fscanf(p, "%lf %lf %lf", &t.referenceNs, &t.candidateNs, &t.speedup) == 3) t.ok = true;
        int rc = pclose(p);
        if (rc != 0 || !t.ok) {
            std::ifstream err(path + ".err");
            std::stringstream ss;
            ss << err.rdbuf();
            t.ok = false;
            t.error = "program timer failed: " + ss.str().substr(0, 300);
        }
        std::remove(path.c_str());
        std::remove((path + ".err").c_str());
        return t;
    }
    // log 208d: struct ABIs are called through an int adapter. Both sources
    // define the structs (the candidate may be the original itself), so the
    // candidate's struct tags and typedef names are renamed by macros.
    std::string src = harnessSource(candidateSource);
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
                      "    long long bad = 0; static int list[" + std::to_string(maxListed) + "];\n"
                      "    for (long long y = -2147483648LL; y <= 2147483647LL; y++)\n"
                      "        if (ref_fn((int)y) != cand_fn((int)y)) { if (bad < " + std::to_string(maxListed) +
                      ") list[bad] = (int)y; bad++; }\n"
                      "    printf(\"%lld\\n\", bad);\n"
                      "    for (long long k = 0; k < bad && k < " + std::to_string(maxListed) +
                      "; k++) printf(\"%d\\n\", list[k]);\n"
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
        if (std::fscanf(p, "%lld", &x.mismatches) == 1) {
            x.complete = true;
            int v = 0;
            while (static_cast<long long>(x.bad.size()) < std::min<long long>(x.mismatches, maxListed)) {
                if (std::fscanf(p, "%d", &v) != 1) {
                    x.complete = false;
                    break;
                }
                x.bad.push_back(v);
            }
            if (!x.bad.empty()) x.firstBad = x.bad[0];
        }
        if (pclose(p) != 0) x.complete = false;
    }
    if (!x.complete) x.error = "did not finish in " + std::to_string(timeoutSeconds) + " s";
    std::remove((base + ".c").c_str());
    std::remove(base.c_str());
    std::remove((base + ".err").c_str());
    return x;
}

}  // namespace algosearch
