#include "problems/ReferenceProblem.hpp"

#include <algorithm>
#include <climits>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>

#include "core/Random.hpp"
#include "problems/ArrayInstances.hpp"

namespace algosearch {

namespace {

constexpr size_t cacheCap = 2'000'000;
constexpr int instancesPerLevel = 40;

const std::vector<int> intEdges = {0,          1,           -1,          2,        -2,       31,
                                   32,         255,         256,         -256,     1 << 16,  (1 << 30),
                                   INT_MAX,    INT_MIN,     INT_MAX - 1, INT_MIN + 1};

}  // namespace

size_t ReferenceProblem::VecHash::operator()(const std::vector<int>& v) const {
    uint64_t h = 1469598103934665603ULL ^ v.size();
    for (int x : v) {
        h ^= static_cast<uint32_t>(x);
        h *= 1099511628211ULL;
    }
    return static_cast<size_t>(h);
}

// Reference runs get a budget of their own (10M by default): the search's
// step limit bounds the candidates, not the specification.
ReferenceProblem::ReferenceProblem(CImport reference, int numVars, uint64_t seed, long referenceStepLimit)
    : reference_(std::move(reference.program)),
      abi_(std::move(reference.abi)),
      numVars_(numVars),
      seed_(seed),
      referenceInterp_(referenceStepLimit) {
    referenceInterp_.setWrapArithmetic(true);
}

std::unique_ptr<ReferenceProblem> ReferenceProblem::fromFile(const std::string& path, const std::string& func,
                                                             int numVars, uint64_t seed) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot read " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return std::make_unique<ReferenceProblem>(importCFunction(ss.str(), func), numVars, seed);
}

int ReferenceProblem::numVars() const { return std::max(numVars_, abi_.numVars); }

int ReferenceProblem::magnitude(int level) {
    if (level >= 30) return INT_MAX;
    return static_cast<int>((1LL << (std::max(level, 0) + 1)) - 1);
}

ReferenceProblem::Run ReferenceProblem::reference(const std::vector<int>& input) const {
    {
        std::shared_lock lock(cacheMutex_);
        auto it = cache_.find(input);
        if (it != cache_.end()) return it->second;
    }
    // Interpreter::run uses one thread-local environment; this is never
    // called while a candidate run is in flight on the same thread (callers
    // classify a run only after it returned), so sharing it is safe.
    std::vector<int> out;
    Run r;
    long steps = 0;
    if (referenceInterp_.run(*reference_, input, abi_.numVars, out, &steps) == RunStatus::COMPLETED) {
        r.result = abiResult(abi_, out);
        r.steps = steps;
    }
    std::unique_lock lock(cacheMutex_);
    if (cache_.size() < cacheCap) cache_.emplace(input, r);
    return r;
}

std::optional<std::vector<int>> ReferenceProblem::expected(const std::vector<int>& input) const {
    return reference(input).result;
}

long ReferenceProblem::referenceSteps(const std::vector<int>& input) const { return reference(input).steps; }

std::vector<Instance> ReferenceProblem::toInstances(const std::vector<std::vector<int>>& inputs) const {
    std::vector<Instance> out;
    for (const auto& in : inputs) {
        if (std::find_if(out.begin(), out.end(), [&](const Instance& i) { return i.input == in; }) != out.end())
            continue;
        std::optional<std::vector<int>> e = expected(in);
        if (e) out.push_back({in, *e});
    }
    return out;
}

std::vector<std::vector<int>> ReferenceProblem::scalarGrid(int lo, int hi) const {
    std::vector<std::vector<int>> out;
    size_t k = abi_.params.size();
    std::vector<int> cur(k, lo);
    for (;;) {
        out.push_back(cur);
        size_t i = 0;
        while (i < k && cur[i] == hi) cur[i++] = lo;
        if (i == k) return out;
        ++cur[i];
    }
}

std::vector<Instance> ReferenceProblem::generateInstances(int level) const {
    Random rng(seed_ + static_cast<uint64_t>(level) * 1000003ULL);
    std::vector<std::vector<int>> inputs;
    if (abi_.kind == CFunctionAbi::Kind::Array) {
        int n = level;
        // n = 0 is a valid C call (log 188: a candidate looping on `v1 - v0`
        // instead of `v1 < v0` read past an empty array); level 1 tests it.
        if (n == 1) inputs.push_back({});
        if (n <= 4) {
            for (auto& a : allArraysOver(n, -1, 2)) inputs.push_back(a);
        } else {
            inputs = randomArraysOver(n, -1, 2, instancesPerLevel / 2, seed_ + static_cast<uint64_t>(n));
        }
        for (auto& a : randomArraysOver(n, -n - 2, n + 2, instancesPerLevel / 2, seed_ + 7919ULL * n)) inputs.push_back(a);
        for (auto& a : generateStructuralArrays(n, seed_, 1, 4)) inputs.push_back(a);
        return toInstances(inputs);
    }
    int m = magnitude(level);
    size_t k = abi_.params.size();
    double gridSize = 1.0;
    for (size_t i = 0; i < k; ++i) gridSize *= 2.0 * m + 1.0;
    if (gridSize <= 81) return toInstances(scalarGrid(-m, m));
    // Values near the bounds and powers of two inside the range, then
    // uniform samples, so a level is not won by small values alone.
    std::vector<int> special = {0, 1, -1, m, -m, m - 1, -m + 1};
    for (int p = 2; p > 0 && p <= m; p *= 2) {
        special.push_back(p);
        special.push_back(-p);
        if (p > INT_MAX / 2) break;
    }
    if (level >= 30) special.push_back(INT_MIN);
    // Every special value appears at least once (rotating over the
    // argument positions), then random combinations of them.
    for (size_t s = 0; s < special.size(); ++s) {
        std::vector<int> args(k);
        for (size_t j = 0; j < k; ++j)
            args[j] = j == s % k ? special[s]
                                 : special[static_cast<size_t>(rng.uniformInt(0, static_cast<int>(special.size()) - 1))];
        inputs.push_back(args);
    }
    for (int i = 0; i < instancesPerLevel / 2; ++i) {
        std::vector<int> args(k);
        for (auto& a : args) a = special[static_cast<size_t>(rng.uniformInt(0, static_cast<int>(special.size()) - 1))];
        inputs.push_back(args);
    }
    for (int i = 0; i < instancesPerLevel; ++i) {
        std::vector<int> args(k);
        for (auto& a : args) a = rng.uniformInt(-m, m);
        inputs.push_back(args);
    }
    return toInstances(inputs);
}

std::string ReferenceProblem::describeLevel(int level) const {
    if (abi_.kind == CFunctionAbi::Kind::Array) return "N = " + std::to_string(level) + " (array length)";
    return "arguments in [-" + std::to_string(magnitude(level)) + ", " + std::to_string(magnitude(level)) + "]";
}

std::vector<Instance> ReferenceProblem::generateAdversarialInstances(int level) const {
    Random rng(seed_ + 999983ULL + static_cast<uint64_t>(level) * 7919ULL);
    std::vector<std::vector<int>> inputs;
    if (abi_.kind == CFunctionAbi::Kind::Array) {
        int n = level;
        inputs = generateStructuralArrays(n, seed_, 2, 8);
        for (int i = 0; i < 10; ++i) {
            std::vector<int> a(static_cast<size_t>(n));
            for (auto& x : a) x = rng.chance(0.5) ? intEdges[rng.uniformInt(0, static_cast<int>(intEdges.size()) - 1)]
                                                  : rng.uniformInt(INT_MIN, INT_MAX);
            inputs.push_back(a);
        }
        return toInstances(inputs);
    }
    // Scalars: the whole int range, whatever the level.
    size_t k = abi_.params.size();
    for (int i = 0; i < 40; ++i) {
        std::vector<int> args(k);
        for (auto& a : args)
            a = rng.chance(0.5) ? intEdges[rng.uniformInt(0, static_cast<int>(intEdges.size()) - 1)]
                                : rng.uniformInt(INT_MIN, INT_MAX);
        inputs.push_back(args);
    }
    return toInstances(inputs);
}

std::vector<std::vector<int>> ReferenceProblem::exhaustiveInputs(int n) const {
    if (abi_.kind == CFunctionAbi::Kind::Array) {
        if (n > 5) return {};
        return allArraysOver(n, -2, 2);
    }
    // Scalar functions have one input size (their arity); verifyGeneralization
    // walks n = 1, 2, ... until an empty list, so everything is returned at
    // n = 1. The grid shrinks with the arity to stay near 40k inputs.
    if (n != 1) return {};
    size_t k = abi_.params.size();
    int r = k == 1 ? 1024 : k == 2 ? 64 : k == 3 ? 16 : 4;
    // Only the reference's domain (log 192: musl's days_in_month shifts by
    // m - 1, undefined outside m in 1..32; a candidate failing exactly where
    // the reference fails is not a generalization failure).
    std::vector<std::vector<int>> grid = scalarGrid(-r, r), in;
    for (auto& g : grid)
        if (expected(g)) in.push_back(std::move(g));
    return in;
}

bool ReferenceProblem::isSolution(const std::vector<int>& input, const std::vector<int>& output) const {
    std::optional<std::vector<int>> e = expected(input);
    if (!e) return true;  // outside the reference's domain
    if (output.size() != input.size()) return false;
    return abiResult(abi_, output) == *e;
}

double ReferenceProblem::partialScore(const std::vector<int>& input, const std::vector<int>& output) const {
    std::optional<std::vector<int>> e = expected(input);
    if (!e) return 1.0;
    if (output.size() != input.size()) return 0.0;
    std::vector<int> got = abiResult(abi_, output);
    if (got == *e) return 1.0;
    // Void array functions: fraction of positions already right. A single
    // int result gets no partial credit (no distance is meaningful for every
    // function).
    if (abi_.returnsValue || e->empty()) return 0.0;
    size_t same = 0;
    for (size_t i = 0; i < e->size(); ++i) same += got[i] == (*e)[i];
    return 0.99 * static_cast<double>(same) / static_cast<double>(e->size());
}

Instance ReferenceProblem::generateBenchmarkInstance(int n, uint64_t seed) const {
    Random rng(seed ^ (static_cast<uint64_t>(n) * 0x9e3779b97f4a7c15ULL));
    std::vector<int> input;
    if (abi_.kind == CFunctionAbi::Kind::Array) {
        input.resize(static_cast<size_t>(std::max(n, 1)));
        for (auto& x : input) x = rng.uniformInt(-n, n);
    } else {
        // n is the magnitude of the arguments (as for gcd / is_prime).
        input.resize(abi_.params.size());
        for (auto& x : input) x = rng.uniformInt(std::max(1, n / 2), std::max(1, n));
    }
    std::optional<std::vector<int>> e = expected(input);
    return {input, e ? *e : std::vector<int>{}};
}

}  // namespace algosearch
