#include "superopt/Superoptimizer.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <memory>
#include <iostream>
#include <optional>
#include <unordered_set>

#include "core/Random.hpp"
#include "interpreter/Interpreter.hpp"
#include "language/PrettyPrinter.hpp"
#include "search/Benchmark.hpp"
#include "superopt/CompiledTimer.hpp"

namespace algosearch {

namespace {

// ------------------------------------------------------------- mutation

struct Slots {
    std::vector<std::vector<StmtPtr>*> lists;  // every statement list, the root's included
    std::vector<ExprPtr*> exprs;               // every expression slot
};

void collectExpr(ExprPtr& e, Slots& out) {
    if (!e) return;
    out.exprs.push_back(&e);
    collectExpr(e->lhs, out);
    collectExpr(e->rhs, out);
}

void collect(Stmt& s, Slots& out) {
    for (ExprPtr* e : {&s.valueExpr, &s.indexExpr, &s.indexExpr2, &s.cond}) collectExpr(*e, out);
    if (s.kind == StmtKind::Block || s.kind == StmtKind::If || s.kind == StmtKind::While) out.lists.push_back(&s.body);
    if (s.kind == StmtKind::If) out.lists.push_back(&s.elseBody);
    for (auto& b : s.body) collect(*b, out);
    for (auto& b : s.elseBody) collect(*b, out);
}

const ExprKind binaryKinds[] = {ExprKind::Add,    ExprKind::Sub,   ExprKind::Mul,    ExprKind::Div,
                                ExprKind::Mod,    ExprKind::BitAnd, ExprKind::BitOr, ExprKind::BitXor,
                                ExprKind::Shl,    ExprKind::Shr,   ExprKind::Lt,     ExprKind::Gt,
                                ExprKind::Le,     ExprKind::Ge,    ExprKind::Eq,     ExprKind::Ne};
const int constants[] = {0, 1, 2, -1, 3, 4, 8, 16, 31, 32};

struct Mutator {
    Random rng;
    int numVars;
    int arity;

    template <class T>
    T& pick(std::vector<T>& v) {
        return v[static_cast<size_t>(rng.uniformInt(0, static_cast<int>(v.size()) - 1))];
    }

    ExprPtr leaf() {
        int r = rng.uniformInt(0, 9);
        if (r < 4) return makeVar(rng.uniformInt(0, numVars - 1));
        if (r < 7) return makeIntConst(constants[rng.uniformInt(0, 9)]);
        if (arity > 0) return makeArrayAccess(makeIntConst(rng.uniformInt(0, arity - 1)));
        return makeArrayAccess(makeVar(rng.uniformInt(0, numVars - 1)));
    }

    ExprPtr expr(int depth) {
        if (depth <= 0 || rng.chance(0.4)) return leaf();
        ExprKind k = binaryKinds[rng.uniformInt(0, 15)];
        return makeBinOp(k, expr(depth - 1), expr(depth - 1));
    }

    StmtPtr assign() { return makeAssign(rng.uniformInt(1, std::max(1, numVars - 1)), expr(2)); }

    // Applies one edit; false when the chosen edit had nothing to act on.
    bool once(Stmt& root) {
        Slots s;
        collect(root, s);
        std::vector<std::vector<StmtPtr>*> nonEmpty;
        for (auto* l : s.lists)
            if (!l->empty()) nonEmpty.push_back(l);
        switch (rng.uniformInt(0, 10)) {
            case 0: {  // delete a statement
                if (nonEmpty.empty()) return false;
                auto& l = *pick(nonEmpty);
                l.erase(l.begin() + rng.uniformInt(0, static_cast<int>(l.size()) - 1));
                return true;
            }
            case 1: {  // replace an expression by a fresh one
                if (s.exprs.empty()) return false;
                *pick(s.exprs) = expr(rng.uniformInt(0, 2));
                return true;
            }
            case 2: {  // change a binary operator
                std::vector<ExprPtr*> bins;
                for (auto* e : s.exprs)
                    if (isBinaryKind((*e)->kind)) bins.push_back(e);
                if (bins.empty()) return false;
                (*pick(bins))->kind = binaryKinds[rng.uniformInt(0, 15)];
                return true;
            }
            case 3: {  // hoist a child over its parent: (x + 0) -> x
                std::vector<ExprPtr*> bins;
                for (auto* e : s.exprs)
                    if ((*e)->lhs && (*e)->rhs) bins.push_back(e);
                if (bins.empty()) return false;
                ExprPtr& e = *pick(bins);
                ExprPtr child = rng.chance(0.5) ? std::move(e->lhs) : std::move(e->rhs);
                e = std::move(child);
                return true;
            }
            case 4: {  // tweak a constant
                std::vector<ExprPtr*> cs;
                for (auto* e : s.exprs)
                    if ((*e)->kind == ExprKind::IntConst) cs.push_back(e);
                if (cs.empty()) return false;
                Expr& c = **pick(cs);
                c.intValue = rng.chance(0.5) ? c.intValue + (rng.chance(0.5) ? 1 : -1) : constants[rng.uniformInt(0, 9)];
                return true;
            }
            case 5: {  // replace an if (or a loop) by one of its bodies
                std::vector<std::pair<std::vector<StmtPtr>*, size_t>> sites;
                for (auto* l : s.lists)
                    for (size_t i = 0; i < l->size(); ++i)
                        if ((*l)[i]->kind == StmtKind::If || (*l)[i]->kind == StmtKind::While) sites.push_back({l, i});
                if (sites.empty()) return false;
                auto [l, i] = sites[static_cast<size_t>(rng.uniformInt(0, static_cast<int>(sites.size()) - 1))];
                StmtPtr st = std::move((*l)[i]);
                std::vector<StmtPtr>& keep = (st->kind == StmtKind::If && rng.chance(0.5)) ? st->elseBody : st->body;
                l->erase(l->begin() + static_cast<long>(i));
                l->insert(l->begin() + static_cast<long>(i), std::make_move_iterator(keep.begin()),
                          std::make_move_iterator(keep.end()));
                return true;
            }
            case 6: {  // insert an assignment
                auto& l = *pick(s.lists);
                l.insert(l.begin() + rng.uniformInt(0, static_cast<int>(l.size())), assign());
                return true;
            }
            case 7: {  // swap two adjacent statements
                std::vector<std::vector<StmtPtr>*> two;
                for (auto* l : s.lists)
                    if (l->size() >= 2) two.push_back(l);
                if (two.empty()) return false;
                auto& l = *pick(two);
                size_t i = static_cast<size_t>(rng.uniformInt(0, static_cast<int>(l.size()) - 2));
                std::swap(l[i], l[i + 1]);
                return true;
            }
            case 8: {  // copy one expression over another
                if (s.exprs.size() < 2) return false;
                ExprPtr copy = (*pick(s.exprs))->clone();
                *pick(s.exprs) = std::move(copy);
                return true;
            }
            case 9: {  // combine an expression with a leaf
                if (s.exprs.empty()) return false;
                ExprPtr& e = *pick(s.exprs);
                ExprPtr old = std::move(e);
                ExprKind k = binaryKinds[rng.uniformInt(0, 9)];  // arithmetic/bitwise only
                e = rng.chance(0.5) ? makeBinOp(k, std::move(old), leaf()) : makeBinOp(k, leaf(), std::move(old));
                return true;
            }
            default: {  // replace a statement by an assignment
                if (nonEmpty.empty()) return false;
                auto& l = *pick(nonEmpty);
                pick(l) = assign();
                return true;
            }
        }
    }
};

// ------------------------------------------------------------ evaluation

std::vector<int> randomInput(Random& rng, const CFunctionAbi& abi, int maxLen) {
    if (abi.kind == CFunctionAbi::Kind::Scalars) {
        std::vector<int> in(abi.params.size());
        for (auto& x : in) x = rng.chance(0.3) ? rng.uniformInt(-64, 64) : rng.uniformInt(INT_MIN, INT_MAX);
        return in;
    }
    std::vector<int> in(static_cast<size_t>(rng.uniformInt(0, maxLen)));
    bool wide = rng.chance(0.3);
    for (auto& x : in) x = wide ? rng.uniformInt(INT_MIN, INT_MAX) : rng.uniformInt(-20, 20);
    return in;
}

struct Candidate {
    Program program;
    std::string key;
    long long cost = 0;
    int fails = 0;
    int size = 0;
    bool fromTimed = false;  // a mutant of a timed, measured-fast parent
};

struct Timed {
    Program program;
    std::string key;
    double speedup = 0;
    long long cost = 0;
};

}  // namespace

Program mutateForSuperopt(const Stmt& program, int numVars, int arity, uint64_t seed) {
    Mutator m{Random(seed), numVars, arity};
    Program p = program.clone();
    int edits = 1;
    while (edits < 4 && m.rng.chance(0.35)) ++edits;
    for (int i = 0; i < edits; ++i)
        for (int tries = 0; tries < 8 && !m.once(*p); ++tries) {
        }
    return p;
}

Superoptimizer::Superoptimizer(const ReferenceProblem& problem, SuperoptConfig config)
    : problem_(problem), config_(std::move(config)) {
    const CFunctionAbi& abi = problem_.abi();
    bool scalars = abi.kind == CFunctionAbi::Kind::Scalars;
    model_ = CostModel::x86Default(scalars);

    // Tests: every level of the reference problem, plus adversarial inputs.
    std::vector<int> levels;
    if (scalars) {
        levels = {1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 24, 28, 30};
    } else {
        for (int n = 1; n <= config_.arrayMaxN; ++n) levels.push_back(n);
    }
    for (int level : levels)
        for (auto& inst : problem_.generateInstances(level)) tests_.push_back(inst);
    for (int level : {levels.front(), levels[levels.size() / 2], levels.back()})
        for (auto& inst : problem_.generateAdversarialInstances(level)) tests_.push_back(inst);

    // Workload for the cost: realistic sizes, not the small test levels.
    Random rng(config_.seed ^ 0x51ed270b27f0a3c5ULL);
    while (workload_.size() < (scalars ? 64u : 24u)) {
        std::vector<int> in;
        if (scalars) {
            in = randomInput(rng, abi, 0);
        } else {
            in.resize(static_cast<size_t>(rng.uniformInt(16, 64)));
            for (auto& x : in) x = rng.uniformInt(-50, 50);
        }
        if (problem_.expected(in)) workload_.push_back(in);
    }
    // Step budgets per input, relative to the reference on that input: a
    // mutant stuck in a loop costs a few times the reference, not a global cap.
    for (const auto& in : workload_) workloadLimits_.push_back(stepBudget(problem_.referenceSteps(in)));
    for (const auto& t : tests_) testLimits_.push_back(stepBudget(problem_.referenceSteps(t.input)));
}

long long Superoptimizer::cost(const Stmt& program) const {
    long long total = 0;
    std::vector<int> out;
    for (size_t i = 0; i < workload_.size(); ++i) {
        const auto& in = workload_[i];
        Interpreter interp(workloadLimits_[i]);
        interp.setWrapArithmetic(true);
        interp.setCostModel(&model_);
        if (interp.run(program, in, problem_.numVars(), out) != RunStatus::COMPLETED) return -1;
        if (!problem_.isSolution(in, out)) return -1;
        total += Interpreter::lastCost();
    }
    // The emitted C copies the caller's array (a load and a store per
    // element) when the program uses it as scratch space (log 188).
    if (emittedCopiesArray(program, problem_.abi()))
        for (const auto& in : workload_) total += 2 * static_cast<long long>(in.size());
    return total;
}

SuperoptResult Superoptimizer::run() {
    using Clock = std::chrono::steady_clock;
    auto start = Clock::now();
    auto elapsed = [&] { return std::chrono::duration<double>(Clock::now() - start).count(); };
    const CFunctionAbi& abi = problem_.abi();
    int arity = abi.kind == CFunctionAbi::Kind::Scalars ? static_cast<int>(abi.params.size()) : 0;
    int numVars = problem_.numVars();
    Random rng(config_.seed);
    // Runs `p` on `in` within `limit` steps; true when it completes and agrees with the reference.
    auto agrees = [&](const Stmt& p, const std::vector<int>& in, long limit, std::vector<int>& out) {
        Interpreter interp(limit);
        interp.setWrapArithmetic(true);
        return interp.run(p, in, numVars, out) == RunStatus::COMPLETED && problem_.isSolution(in, out);
    };

    SuperoptResult result;
    result.workload = static_cast<int>(workload_.size());
    std::vector<size_t> order(tests_.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::vector<int> out;

    // Failed tests, stopping once more than `cap` failed. A failing test
    // moves to the front so the next wrong candidate fails fast.
    auto failures = [&](const Stmt& p, int cap) {
        int fails = 0;
        for (size_t k = 0; k < order.size(); ++k) {
            const Instance& t = tests_[order[k]];
            bool ok = agrees(p, t.input, testLimits_[order[k]], out);
            if (!ok) {
                ++fails;
                if (k > 0) std::rotate(order.begin(), order.begin() + static_cast<long>(k),
                                       order.begin() + static_cast<long>(k) + 1);
                if (fails > cap) return fails;
            }
        }
        return fails;
    };

    // CEGIS: an input on which `p` disagrees with the reference, if any.
    auto counterexample = [&](const Stmt& p) -> std::optional<std::vector<int>> {
        auto bad = [&](const std::vector<int>& in) {
            long steps = problem_.referenceSteps(in);
            if (steps < 0) return false;  // outside the domain
            return !agrees(p, in, stepBudget(steps), out);
        };
        if (abi.kind == CFunctionAbi::Kind::Array && bad({})) return std::vector<int>{};
        for (int n = 1;; ++n) {
            auto inputs = problem_.exhaustiveInputs(n);
            if (inputs.empty()) break;
            for (const auto& in : inputs)
                if (bad(in)) return in;
        }
        Random r(rng.engine()());
        for (int i = 0; i < 2000; ++i) {
            std::vector<int> in = randomInput(r, abi, 40);
            if (bad(in)) return in;
        }
        return std::nullopt;
    };

    std::unordered_set<std::string> seen;
    std::vector<Candidate> beam, explore;
    {
        Candidate ref;
        ref.program = problem_.referenceProgram().clone();
        ref.key = printStmt(*ref.program);
        ref.cost = cost(*ref.program);
        ref.size = ref.program->size();
        result.referenceCost = ref.cost;
        seen.insert(ref.key);
        beam.push_back(std::move(ref));
    }
    long long bestCost = result.referenceCost;
    int maxSize = beam.front().size * 2 + 20;
    int failCap = std::max(1, static_cast<int>(config_.maxFailFraction * static_cast<double>(tests_.size())));
    result.trajectory.push_back({0.0, bestCost});

    // Phase 3b: compiled timing against the original, in one process.
    std::unique_ptr<CompiledTimer> timer;
    std::vector<Timed> timed;
    std::unordered_set<std::string> timedKeys;
    auto emit = [&](const Stmt& p) -> std::optional<std::string> {
        try {
            return emitCFunction(p, abi);
        } catch (const CImportError&) {
            return std::nullopt;
        }
    };
    if (config_.timeTopK > 0) {
        timer = std::make_unique<CompiledTimer>(config_.originalSource, abi, workload_, config_.workDir);
        double spread = 0;
        std::vector<double> self;
        for (int i = 0; i < 3; ++i) {
            CompiledTimer::Timing t = timer->time(config_.originalSource);
            if (!t.ok) throw CImportError("compiled timing failed: " + t.error);
            self.push_back(t.speedup);
            spread = std::max(spread, std::abs(1.0 - t.speedup));
        }
        std::sort(self.begin(), self.end());
        result.selfControl = self[1];
        result.noiseThreshold = std::max(0.03, 2.0 * spread);
        if (auto c = emit(problem_.referenceProgram())) {
            CompiledTimer::Timing t = timer->time(*c);
            if (t.ok) result.emittedReference = t.speedup;
        }
        result.timings = 4;
    }
    // Times up to timeTopK untimed correct candidates of the beam: the
    // model's best, mutants of measured-fast parents, then random ones (the
    // model alone is a poor guide, log 188b). Each is checked for
    // counterexamples first, so the timed archive holds only programs that
    // passed the same CEGIS as a model best.
    auto timeSome = [&](std::vector<Candidate>& pool) {
        if (!timer || pool.empty()) return;
        if (result.timingSeconds > config_.timeFraction * std::max(1.0, elapsed())) return;
        std::vector<size_t> picks;
        auto untimed = [&](size_t i) {
            return !timedKeys.count(pool[i].key) &&
                   std::find(picks.begin(), picks.end(), i) == picks.end();
        };
        for (size_t i = 0; i < pool.size() && picks.size() < 1; ++i)
            if (untimed(i)) picks.push_back(i);
        for (size_t i = 0; i < pool.size() && picks.size() < static_cast<size_t>(1 + config_.timeTopK / 2); ++i)
            if (pool[i].fromTimed && untimed(i)) picks.push_back(i);
        for (int tries = 0; tries < 4 * config_.timeTopK && picks.size() < static_cast<size_t>(config_.timeTopK);
             ++tries) {
            size_t i = static_cast<size_t>(rng.uniformInt(0, static_cast<int>(pool.size()) - 1));
            if (untimed(i)) picks.push_back(i);
        }
        auto t0 = Clock::now();
        for (size_t i : picks) {
            const Candidate& c = pool[i];
            timedKeys.insert(c.key);
            if (counterexample(*c.program)) continue;
            auto src = emit(*c.program);
            if (!src) continue;
            CompiledTimer::Timing t = timer->time(*src);
            ++result.timings;
            if (!t.ok) continue;
            timed.push_back({c.program->clone(), c.key, t.speedup, c.cost});
            if (config_.verbose)
                std::cout << "[superopt] timed " << t.speedup << "x (model cost " << c.cost << ", size "
                          << c.program->size() << ")\n";
        }
        std::sort(timed.begin(), timed.end(), [](const Timed& a, const Timed& b) { return a.speedup > b.speedup; });
        result.timingSeconds += std::chrono::duration<double>(Clock::now() - t0).count();
    };

    while (elapsed() < config_.wallSeconds) {
        std::vector<const Candidate*> parents;
        for (const auto& c : beam) parents.push_back(&c);
        for (const auto& c : explore) parents.push_back(&c);
        std::vector<Candidate> timedParents;
        for (size_t i = 0; i < timed.size() && i < static_cast<size_t>(config_.timedParents); ++i) {
            if (timed[i].speedup <= 1.0) break;
            Candidate c;
            c.program = timed[i].program->clone();
            c.key = timed[i].key;
            c.fromTimed = true;
            timedParents.push_back(std::move(c));
        }
        for (const auto& c : timedParents) parents.push_back(&c);
        std::vector<Candidate> fresh, freshExplore;
        for (const Candidate* parent : parents) {
            for (int m = 0; m < config_.mutantsPerParent; ++m) {
                Candidate c;
                c.program = mutateForSuperopt(*parent->program, numVars, arity, rng.engine()());
                c.fromTimed = parent->fromTimed;
                c.size = c.program->size();
                if (c.size > maxSize) continue;
                c.key = printStmt(*c.program);
                if (!seen.insert(c.key).second) continue;
                ++result.evaluated;
                c.fails = failures(*c.program, failCap);
                if (c.fails == 0) {
                    c.cost = cost(*c.program);
                    if (c.cost < 0) continue;
                    ++result.correct;
                    fresh.push_back(std::move(c));
                } else if (c.fails <= failCap) {
                    freshExplore.push_back(std::move(c));
                }
            }
        }
        for (auto& c : fresh) beam.push_back(std::move(c));
        std::sort(beam.begin(), beam.end(), [](const Candidate& a, const Candidate& b) {
            return a.cost != b.cost ? a.cost < b.cost : a.size < b.size;
        });
        // A new best must survive the counterexample search; a counterexample
        // becomes a test and every correct candidate is re-checked against it.
        while (!beam.empty() && beam.front().cost < bestCost) {
            auto cex = counterexample(*beam.front().program);
            if (!cex) {
                bestCost = beam.front().cost;
                result.trajectory.push_back({elapsed(), bestCost});
                if (config_.verbose)
                    std::cout << "[superopt] " << elapsed() << " s: cost " << bestCost << " (size "
                              << beam.front().size << ")\n";
                break;
            }
            tests_.push_back({*cex, *problem_.expected(*cex)});
            testLimits_.push_back(stepBudget(problem_.referenceSteps(*cex)));
            order.insert(order.begin(), tests_.size() - 1);
            ++result.counterexamples;
            std::vector<Candidate> kept;
            for (auto& c : beam)
                if (failures(*c.program, 0) == 0) kept.push_back(std::move(c));
            beam = std::move(kept);
        }
        if (beam.empty()) {  // every candidate fell to counterexamples: restart from the reference
            Candidate ref;
            ref.program = problem_.referenceProgram().clone();
            ref.key = printStmt(*ref.program);
            ref.cost = result.referenceCost;
            ref.size = ref.program->size();
            beam.push_back(std::move(ref));
        }
        if (beam.size() > static_cast<size_t>(config_.beamWidth)) beam.resize(static_cast<size_t>(config_.beamWidth));
        timeSome(beam);
        for (auto& c : freshExplore) explore.push_back(std::move(c));
        std::sort(explore.begin(), explore.end(), [](const Candidate& a, const Candidate& b) {
            return a.fails != b.fails ? a.fails < b.fails : a.size < b.size;
        });
        // Keep exploration varied: random survivors, biased towards the front.
        if (explore.size() > static_cast<size_t>(config_.exploreWidth)) {
            std::vector<Candidate> kept;
            size_t half = static_cast<size_t>(config_.exploreWidth) / 2;
            for (size_t i = 0; i < half; ++i) kept.push_back(std::move(explore[i]));
            while (kept.size() < static_cast<size_t>(config_.exploreWidth)) {
                size_t j = half + static_cast<size_t>(rng.uniformInt(0, static_cast<int>(explore.size() - half) - 1));
                if (explore[j].program) kept.push_back(std::move(explore[j]));
            }
            explore = std::move(kept);
        }
    }

    // The cheapest candidate with no counterexample (the reference always qualifies).
    result.best = problem_.referenceProgram().clone();
    result.bestCost = result.referenceCost;
    for (const auto& c : beam) {
        if (c.cost >= result.bestCost) break;
        if (!counterexample(*c.program)) {
            result.best = c.program->clone();
            result.bestCost = c.cost;
            break;
        }
        ++result.counterexamples;
    }
    result.tests = static_cast<int>(tests_.size());
    result.modelBest = result.best->clone();
    result.modelBestCost = result.bestCost;
    if (timer) {
        // Re-measure the fastest few (3 runs of 21 trials each, median) and
        // keep one only if it beats the noise threshold; otherwise the answer
        // is "no measured gain": the reference.
        result.best = problem_.referenceProgram().clone();
        result.bestCost = result.referenceCost;
        result.measuredSpeedup = 1.0;
        for (size_t k = 0; k < timed.size() && k < 3; ++k) {
            if (timed[k].speedup <= 1.0 + result.noiseThreshold) break;
            auto src = emit(*timed[k].program);
            std::vector<double> r;
            for (int rep = 0; rep < 3 && src; ++rep) {
                CompiledTimer::Timing t = timer->time(*src, 21);
                ++result.timings;
                if (t.ok) r.push_back(t.speedup);
            }
            if (r.size() < 3) continue;
            std::sort(r.begin(), r.end());
            if (r[1] > 1.0 + result.noiseThreshold && r[1] > result.measuredSpeedup) {
                result.best = timed[k].program->clone();
                result.bestCost = timed[k].cost;
                result.measuredSpeedup = r[1];
            }
        }
    }
    // verifyGeneralization samples up to n = 80 and magnitude-shifted inputs:
    // budget from the reference's steps there, with a margin for the shift
    // (log 188: the n <= 10 test budget timed out the reference itself).
    long maxLimit = *std::max_element(testLimits_.begin(), testLimits_.end());
    for (const auto& inst : problem_.generateInstances(80))
        maxLimit = std::max(maxLimit, 4 * stepBudget(std::max(0L, problem_.referenceSteps(inst.input))));
    result.verified = verifyGeneralization(problem_, *result.best, maxLimit, result.verifyReason, false);
    return result;
}

}  // namespace algosearch
