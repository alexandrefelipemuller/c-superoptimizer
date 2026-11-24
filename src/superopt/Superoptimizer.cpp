#include "superopt/Superoptimizer.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <memory>
#include <iostream>
#include <optional>
#include <set>
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

bool isComparison(ExprKind k) {
    return k == ExprKind::Lt || k == ExprKind::Gt || k == ExprKind::Le || k == ExprKind::Ge || k == ExprKind::Eq ||
           k == ExprKind::Ne;
}

void collectVars(const Expr* e, std::set<int>& out) {
    if (!e) return;
    if (e->kind == ExprKind::Var) out.insert(e->varIndex);
    collectVars(e->lhs.get(), out);
    collectVars(e->rhs.get(), out);
}

void collectConsts(const Expr* e, std::set<long long>& out);

// Log 203: which branch of an if/else sets a result to a constant while the
// other sets the same result (a variable, or arr[k]) to a non-constant
// expression, each optionally followed by `return`: 0 the then-branch, 1 the
// else-branch, -1 neither.
int constantBranch(const Stmt& st) {
    if (st.body.empty() || st.body.size() != st.elseBody.size() || st.body.size() > 2) return -1;
    if (st.body.size() == 2 &&
        (st.body[1]->kind != StmtKind::Return || st.elseBody[1]->kind != StmtKind::Return))
        return -1;
    const Stmt& a = *st.body[0];
    const Stmt& b = *st.elseBody[0];
    if (a.kind != b.kind) return -1;
    if (a.kind == StmtKind::Assign) {
        if (a.varIndex != b.varIndex) return -1;
    } else if (a.kind == StmtKind::AssignArray) {
        if (a.indexExpr->kind != ExprKind::IntConst || b.indexExpr->kind != ExprKind::IntConst ||
            a.indexExpr->intValue != b.indexExpr->intValue)
            return -1;
    } else {
        return -1;
    }
    bool ka = a.valueExpr->kind == ExprKind::IntConst, kb = b.valueExpr->kind == ExprKind::IntConst;
    if (ka == kb) return -1;
    return ka ? 0 : 1;
}

// Log 203: `x % c == 0` or `!= 0` (either side order) with c = 2^p * b, p >= 1, b > 1 odd.
bool splittableModTest(const Expr& e) {
    if (e.kind != ExprKind::Eq && e.kind != ExprKind::Ne) return false;
    const Expr* mod = e.lhs->kind == ExprKind::Mod ? e.lhs.get() : e.rhs->kind == ExprKind::Mod ? e.rhs.get() : nullptr;
    const Expr* zero = mod == e.lhs.get() ? e.rhs.get() : e.lhs.get();
    if (!mod || zero->kind != ExprKind::IntConst || zero->intValue != 0) return false;
    if (mod->rhs->kind != ExprKind::IntConst) return false;
    int c = mod->rhs->intValue;
    if (c <= 0 || c % 2 != 0) return false;
    while (c % 2 == 0) c /= 2;
    return c > 1;
}


// ------------------------------------------------------------- idiom rewrites (log 208)
// Classical rewrites of whole loop shapes, applied wherever the shape occurs
// (opt-in, `--idiom-moves`): subtractive division, counted arithmetic sum,
// bit-serial loops (popcount, parity, bit reversal) and power-of-two doubling.

ExprPtr V(int v) { return makeVar(v); }
ExprPtr K(int c) { return makeIntConst(c); }
ExprPtr B(ExprKind k, ExprPtr a, ExprPtr b) { return makeBinOp(k, std::move(a), std::move(b)); }

bool isVarE(const Expr* e, int v) { return e && e->kind == ExprKind::Var && e->varIndex == v; }
bool isConstE(const Expr* e, int c) { return e && e->kind == ExprKind::IntConst && e->intValue == c; }

// `v = v + 1` or `v = 1 + v`.
bool isIncrement(const Stmt& st, int v) {
    if (st.kind != StmtKind::Assign || st.varIndex != v || st.valueExpr->kind != ExprKind::Add) return false;
    const Expr& e = *st.valueExpr;
    return (isVarE(e.lhs.get(), v) && isConstE(e.rhs.get(), 1)) || (isVarE(e.rhs.get(), v) && isConstE(e.lhs.get(), 1));
}

// `v = v OP w` (or `w OP v` for a commutative OP); returns w or null.
const Expr* accumulateOperand(const Stmt& st, int v, ExprKind op) {
    if (st.kind != StmtKind::Assign || st.varIndex != v || st.valueExpr->kind != op) return nullptr;
    const Expr& e = *st.valueExpr;
    if (isVarE(e.lhs.get(), v)) return e.rhs.get();
    if (op != ExprKind::Sub && isVarE(e.rhs.get(), v)) return e.lhs.get();
    return nullptr;
}

void varsOf(const Stmt& s, std::set<int>& out) {
    if (s.kind == StmtKind::Assign) out.insert(s.varIndex);
    for (const Expr* e : {s.valueExpr.get(), s.indexExpr.get(), s.indexExpr2.get(), s.cond.get()}) collectVars(e, out);
    for (const auto& b : s.body) varsOf(*b, out);
    for (const auto& b : s.elseBody) varsOf(*b, out);
}

// Whether the last write of `v` before position `i` of `list` is `v = value`,
// with only plain scalar assignments in between.
bool initialisedTo(const std::vector<StmtPtr>& list, size_t i, int v, int value) {
    while (i-- > 0) {
        const Stmt& st = *list[i];
        if (st.kind != StmtKind::Assign) return false;
        if (st.varIndex == v) return isConstE(st.valueExpr.get(), value);
    }
    return false;
}

bool exprUsesVar(const Expr* e, int v) {
    std::set<int> vs;
    collectVars(e, vs);
    return vs.count(v) > 0;
}

bool stmtUsesVar(const Stmt& st, int v) {
    std::set<int> vs;
    varsOf(st, vs);
    return vs.count(v) > 0;
}

bool readsArray(const Expr* e) {
    if (!e) return false;
    if (e->kind == ExprKind::ArrayAccess || e->kind == ExprKind::AuxAccess) return true;
    return readsArray(e->lhs.get()) || readsArray(e->rhs.get());
}

// Whether `st` writes the array other than in a block that then returns,
// or writes scalar variable `v` (v < 0: no variable check).
bool writesArrayOrVar(const Stmt& st, int v) {
    if (st.kind == StmtKind::Assign) return st.varIndex == v;
    if (st.kind == StmtKind::AssignArray || st.kind == StmtKind::AssignAux || st.kind == StmtKind::Swap ||
        st.kind == StmtKind::Call)
        return true;
    auto list = [&](const std::vector<StmtPtr>& l) {
        bool returns = !l.empty() && l.back()->kind == StmtKind::Return;
        for (const auto& b : l) {
            if (b->kind == StmtKind::Assign && b->varIndex == v) return true;
            if (b->kind == StmtKind::Return) continue;
            if (b->kind == StmtKind::AssignArray && returns) continue;
            if (writesArrayOrVar(*b, v)) return true;
        }
        return false;
    };
    return list(st.body) || list(st.elseBody);
}

// `v = v + 1` at the end of `body`, `while (v < N)`: the counted loop shape.
bool countedLoop(const Stmt& w, int& v, const Expr*& n) {
    if (w.kind != StmtKind::While || w.cond->kind != ExprKind::Lt || w.cond->lhs->kind != ExprKind::Var) return false;
    v = w.cond->lhs->varIndex;
    n = w.cond->rhs.get();
    if (n->kind != ExprKind::Var && n->kind != ExprKind::IntConst) return false;
    if (isVarE(n, v) || w.body.empty() || !isIncrement(*w.body.back(), v)) return false;
    return true;
}

// The comparison `cond` as arr[j] OP x with x free of j (OP normalised so arr[j]
// is on the left): returns OP, or Add when `cond` is not of that form.
ExprKind normalisedArrayCompare(const Expr& cond, int j, const Expr*& other) {
    auto isAj = [&](const Expr* e) { return e->kind == ExprKind::ArrayAccess && isVarE(e->lhs.get(), j); };
    static const std::pair<ExprKind, ExprKind> flip[] = {{ExprKind::Lt, ExprKind::Gt}, {ExprKind::Gt, ExprKind::Lt},
                                                        {ExprKind::Le, ExprKind::Ge}, {ExprKind::Ge, ExprKind::Le}};
    for (auto [k, f] : flip) {
        if (cond.kind != k) continue;
        if (isAj(cond.lhs.get()) && !exprUsesVar(cond.rhs.get(), j)) { other = cond.rhs.get(); return k; }
        if (isAj(cond.rhs.get()) && !exprUsesVar(cond.lhs.get(), j)) { other = cond.lhs.get(); return f; }
    }
    return ExprKind::Add;
}

// t = OP-fold steps: t = t OP ((t >> k) [& mask]).
StmtPtr foldStep(int t, ExprKind op, int k) { return makeAssign(t, B(op, V(t), B(ExprKind::Shr, V(t), K(k)))); }

void appendPopcount(std::vector<StmtPtr>& out, int t) {
    out.push_back(makeAssign(t, B(ExprKind::Sub, V(t), B(ExprKind::BitAnd, B(ExprKind::Shr, V(t), K(1)), K(0x55555555)))));
    out.push_back(makeAssign(t, B(ExprKind::Add, B(ExprKind::BitAnd, V(t), K(0x33333333)),
                                  B(ExprKind::BitAnd, B(ExprKind::Shr, V(t), K(2)), K(0x33333333)))));
    out.push_back(makeAssign(t, B(ExprKind::BitAnd, B(ExprKind::Add, V(t), B(ExprKind::Shr, V(t), K(4))), K(0x0f0f0f0f))));
    out.push_back(makeAssign(t, B(ExprKind::Shr, B(ExprKind::Mul, V(t), K(0x01010101)), K(24))));
}

void appendReverse32(std::vector<StmtPtr>& out, int t) {
    const int masks[] = {0x55555555, 0x33333333, 0x0f0f0f0f, 0x00ff00ff};
    for (int j = 0; j < 4; ++j) {
        int k = 1 << j;
        out.push_back(makeAssign(t, B(ExprKind::BitOr, B(ExprKind::BitAnd, B(ExprKind::Shr, V(t), K(k)), K(masks[j])),
                                      B(ExprKind::Shl, B(ExprKind::BitAnd, V(t), K(masks[j])), K(k)))));
    }
    out.push_back(makeAssign(t, B(ExprKind::BitOr, B(ExprKind::BitAnd, B(ExprKind::Shr, V(t), K(16)), K(0xffff)),
                                  B(ExprKind::Shl, V(t), K(16)))));
}

struct Mutator {
    Random rng;
    int numVars;
    int arity;
    bool algorithmic = false;  // phase 3c moves (log 190)
    bool returnsValue = true;
    bool reach = false;        // log 203: branch merge and modulus split among the phase 3c moves
    bool idiom = false;        // log 208: the classical idiom rewrites (moves 7-10)
    bool structural = false;   // some applied edit was a phase 3c move
    bool idiomApplied = false; // some applied edit was an idiom rewrite

    // A variable the program never mentions (not v0), or -1.
    int freeVar(const Stmt& root) const {
        std::set<int> used;
        varsOf(root, used);
        for (int v = numVars - 1; v >= 1; --v)
            if (!used.count(v)) return v;
        return -1;
    }

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

    // Statement sites of a given kind: (list, index).
    std::vector<std::pair<std::vector<StmtPtr>*, size_t>> sites(const Slots& s, StmtKind kind) {
        std::vector<std::pair<std::vector<StmtPtr>*, size_t>> out;
        for (auto* l : s.lists)
            for (size_t i = 0; i < l->size(); ++i)
                if ((*l)[i]->kind == kind) out.push_back({l, i});
        return out;
    }

    ExprPtr condition() {
        static const ExprKind cmp[] = {ExprKind::Lt, ExprKind::Gt, ExprKind::Le, ExprKind::Ge, ExprKind::Eq,
                                       ExprKind::Ne};
        return makeBinOp(cmp[rng.uniformInt(0, 5)], expr(1), expr(1));
    }

    // Log 208c: move 11, a nested O(n^2) loop over the array -> O(n).
    // F: the inner loop scans the whole array, `if (arr[j] OP x) { constant
    //    assignments }`: exists j with arr[j] > x <=> max(arr) > x, so the max
    //    (min for < <=) is computed once before the outer loop.
    // E: for i, for j = i + 1 ..: if (arr[i] OP arr[j]) { const result; return }
    //    -> one pass with the running max (min) of the prefix.
    bool nestedOnce(Slots& s, const Stmt& root) {
        std::set<int> used;
        varsOf(root, used);
        std::vector<int> fresh;
        for (int v = numVars - 1; v >= 1 && fresh.size() < 2; --v)
            if (!used.count(v)) fresh.push_back(v);
        std::vector<std::pair<std::vector<StmtPtr>*, size_t>> ok;
        std::vector<std::vector<StmtPtr>> repl;
        for (auto [l, oi] : sites(s, StmtKind::While)) {
            const Stmt& outer = *(*l)[oi];
            int vi;
            const Expr* nO;
            if (!countedLoop(outer, vi, nO)) continue;
            for (size_t k = 0; k + 1 < outer.body.size(); ++k) {
                const Stmt& inner = *outer.body[k];
                int vj;
                const Expr* nI;
                if (!countedLoop(inner, vj, nI) || vj == vi || inner.body.size() != 2 || k == 0) continue;
                const Stmt& test = *inner.body[0];
                if (test.kind != StmtKind::If || !test.elseBody.empty() || test.body.empty()) continue;
                const Stmt& init = *outer.body[k - 1];
                if (init.kind != StmtKind::Assign || init.varIndex != vj) continue;
                const Expr* x = nullptr;
                ExprKind op = normalisedArrayCompare(*test.cond, vj, x);
                if (op == ExprKind::Add) continue;
                bool wantMax = op == ExprKind::Gt || op == ExprKind::Ge;
                // nothing in the outer loop writes the array (except right before a return) or the bounds
                bool clean = true;
                for (const auto& b : outer.body) {
                    if (b.get() == &inner) continue;
                    if (writesArrayOrVar(*b, nO->kind == ExprKind::Var ? nO->varIndex : -1)) clean = false;
                    if (nI->kind == ExprKind::Var && b->kind == StmtKind::Assign && b->varIndex == nI->varIndex) clean = false;
                }
                if (!clean) continue;
                std::vector<StmtPtr> out;
                bool full = isConstE(init.valueExpr.get(), 0) && exprEqual(nI, nO);
                bool constAssigns = true;
                for (const auto& b : test.body)
                    if (b->kind != StmtKind::Assign || b->valueExpr->kind != ExprKind::IntConst || b->varIndex == vj ||
                        b->varIndex == vi)
                        constAssigns = false;
                if (full && constAssigns && fresh.size() == 2 && !exprUsesVar(x, vj)) {
                    // F: t = max/min of arr[0 .. N); the inner loop becomes one test.
                    // (only when N > 0: arr[0] is outside an empty array)
                    int t = fresh[0], q = fresh[1];
                    std::vector<StmtPtr> pre;
                    pre.push_back(makeAssign(t, makeArrayAccess(K(0))));
                    pre.push_back(makeAssign(q, K(1)));
                    std::vector<StmtPtr> upd;
                    upd.push_back(makeAssign(t, makeArrayAccess(V(q))));
                    std::vector<StmtPtr> body;
                    body.push_back(makeIf(B(wantMax ? ExprKind::Gt : ExprKind::Lt, makeArrayAccess(V(q)), V(t)), std::move(upd)));
                    body.push_back(makeAssign(q, B(ExprKind::Add, V(q), K(1))));
                    pre.push_back(makeWhile(B(ExprKind::Lt, V(q), nO->clone()), std::move(body)));
                    out.push_back(makeIf(B(ExprKind::Lt, K(0), nO->clone()), std::move(pre)));
                    Program o = outer.clone();
                    std::vector<StmtPtr> setsBody;
                    for (const auto& b : test.body) setsBody.push_back(b->clone());
                    std::vector<StmtPtr> repInner;
                    repInner.push_back(makeIf(B(op, V(t), x->clone()), std::move(setsBody)));
                    std::vector<StmtPtr> jEnd;
                    jEnd.push_back(makeAssign(vj, nI->clone()));
                    repInner.push_back(makeIf(B(ExprKind::Lt, V(vj), nI->clone()), std::move(jEnd)));
                    o->body.erase(o->body.begin() + static_cast<long>(k));
                    o->body.insert(o->body.begin() + static_cast<long>(k), std::make_move_iterator(repInner.begin()),
                                   std::make_move_iterator(repInner.end()));
                    out.push_back(std::move(o));
                } else {
                    // E: outer body is exactly { j = i + 1; inner; i++ }, the test returns a result free of i, j.
                    if (outer.body.size() != 3 || k != 1 || fresh.empty()) continue;
                    const Expr& iv = *init.valueExpr;
                    if (!(iv.kind == ExprKind::Add && isVarE(iv.lhs.get(), vi) && isConstE(iv.rhs.get(), 1))) continue;
                    if (!exprEqual(nI, nO) || x->kind != ExprKind::ArrayAccess || !isVarE(x->lhs.get(), vi)) continue;
                    if (test.body.back()->kind != StmtKind::Return) continue;
                    bool free = true;
                    for (const auto& b : test.body) {
                        if (stmtUsesVar(*b, vi) || stmtUsesVar(*b, vj) || readsArray(b->valueExpr.get())) free = false;
                        if (b->kind == StmtKind::AssignArray && readsArray(b->indexExpr.get())) free = false;
                    }
                    if (!free) continue;
                    // exists i < j with arr[j] OP arr[i] <=> exists j with arr[j] OP best(arr[i0 .. j)),
                    // best the max for < <= (the prefix side is the right operand), the min for > >=.
                    wantMax = !wantMax;
                    int t = fresh[0];
                    std::vector<StmtPtr> body;
                    body.push_back(makeAssign(t, makeArrayAccess(V(vi))));
                    body.push_back(makeAssign(vj, B(ExprKind::Add, V(vi), K(1))));
                    std::vector<StmtPtr> loop;
                    std::vector<StmtPtr> hit;
                    for (const auto& b : test.body) hit.push_back(b->clone());
                    loop.push_back(makeIf(B(op, makeArrayAccess(V(vj)), V(t)), std::move(hit)));
                    std::vector<StmtPtr> upd;
                    upd.push_back(makeAssign(t, makeArrayAccess(V(vj))));
                    loop.push_back(makeIf(B(wantMax ? ExprKind::Gt : ExprKind::Lt, makeArrayAccess(V(vj)), V(t)), std::move(upd)));
                    loop.push_back(makeAssign(vj, B(ExprKind::Add, V(vj), K(1))));
                    body.push_back(makeWhile(B(ExprKind::Lt, V(vj), nI->clone()), std::move(loop)));
                    body.push_back(makeAssign(vi, nO->clone()));
                    out.push_back(makeIf(outer.cond->clone(), std::move(body)));
                }
                ok.push_back({l, oi});
                repl.push_back(std::move(out));
                break;
            }
        }
        if (ok.empty()) return false;
        size_t k = static_cast<size_t>(rng.uniformInt(0, static_cast<int>(ok.size()) - 1));
        auto [l, i] = ok[k];
        l->erase(l->begin() + static_cast<long>(i));
        l->insert(l->begin() + static_cast<long>(i), std::make_move_iterator(repl[k].begin()),
                  std::make_move_iterator(repl[k].end()));
        return true;
    }

    // Log 208: one classical idiom rewrite (7 subtractive division, 8 counted
    // sum, 9 bit-serial loop, 10 doubling loop); false when the shape is absent.
    bool idiomOnce(Slots& s, const Stmt& root, int which) {
        if (which == 11) return nestedOnce(s, root);
        std::vector<std::pair<std::vector<StmtPtr>*, size_t>> ok;
        std::vector<std::vector<StmtPtr>> repl;
        int t = freeVar(root);
        for (auto [l, i] : sites(s, StmtKind::While)) {
            const Stmt& w = *(*l)[i];
            const Expr& c = *w.cond;
            std::vector<StmtPtr> out;
            if (which == 7) {  // while (A >= B) { A = A - B; C = C + 1; } -> if (A >= B) { C += A / B; A %= B; }
                if (c.kind != ExprKind::Ge || c.lhs->kind != ExprKind::Var || w.body.size() != 2) continue;
                int a = c.lhs->varIndex;
                const Expr* bE = c.rhs.get();
                if (bE->kind != ExprKind::Var && bE->kind != ExprKind::IntConst) continue;
                int cnt = -1;
                bool sub = false;
                for (const auto& st : w.body) {
                    const Expr* d = accumulateOperand(*st, a, ExprKind::Sub);
                    if (d && exprEqual(d, bE)) sub = true;
                    else if (st->kind == StmtKind::Assign && st->varIndex != a && isIncrement(*st, st->varIndex))
                        cnt = st->varIndex;
                }
                if (!sub || cnt < 0 || isVarE(bE, cnt)) continue;
                std::vector<StmtPtr> body;
                body.push_back(makeAssign(cnt, B(ExprKind::Add, V(cnt), B(ExprKind::Div, V(a), bE->clone()))));
                body.push_back(makeAssign(a, B(ExprKind::Mod, V(a), bE->clone())));
                out.push_back(makeIf(c.clone(), std::move(body)));
            } else if (which == 8) {  // while (i <= m) { s = s + i; i = i + 1; } -> Gauss
                if ((c.kind != ExprKind::Le && c.kind != ExprKind::Lt) || c.lhs->kind != ExprKind::Var) continue;
                if (w.body.size() != 2) continue;
                int iv = c.lhs->varIndex;
                const Expr* nE = c.rhs.get();
                if (nE->kind != ExprKind::Var && nE->kind != ExprKind::IntConst) continue;
                if (isVarE(nE, iv) || !isIncrement(*w.body[1], iv) || w.body[0]->kind != StmtKind::Assign) continue;
                int sv = w.body[0]->varIndex;
                const Expr* d = accumulateOperand(*w.body[0], sv, ExprKind::Add);
                if (sv == iv || !isVarE(d, iv) || isVarE(nE, sv)) continue;
                auto m = [&]() { return c.kind == ExprKind::Le ? nE->clone() : B(ExprKind::Sub, nE->clone(), K(1)); };
                auto a = [&]() { return B(ExprKind::Add, V(iv), m()); };
                auto b = [&]() { return B(ExprKind::Add, B(ExprKind::Sub, m(), V(iv)), K(1)); };
                // one of a = i + m, b = m - i + 1 is even (a + b is odd): halve that one, exact under wrapping.
                auto odd = [&]() { return B(ExprKind::BitAnd, a(), K(1)); };
                ExprPtr prod = B(ExprKind::Mul, B(ExprKind::Shr, a(), B(ExprKind::Sub, K(1), odd())),
                                 B(ExprKind::Shr, b(), odd()));
                std::vector<StmtPtr> body;
                body.push_back(makeAssign(sv, B(ExprKind::Add, V(sv), std::move(prod))));
                body.push_back(makeAssign(iv, B(ExprKind::Add, m(), K(1))));
                out.push_back(makeIf(B(ExprKind::Le, V(iv), m()), std::move(body)));
            } else if (which == 9) {  // for (i = 0; i < W; i++) acc = acc (+|^|<<1|) bit(x, i) -> SWAR
                if (t < 0 || c.kind != ExprKind::Lt || c.lhs->kind != ExprKind::Var || c.rhs->kind != ExprKind::IntConst)
                    continue;
                int iv = c.lhs->varIndex, W = c.rhs->intValue;
                if (W < 1 || W > 32 || !initialisedTo(*l, i, iv, 0)) continue;
                int inc = -1, shiftAt = -1, accAt = -1, acc = -1, x = -1, kind = -1;  // kind 0 +, 1 ^, 2 reverse
                bool shifted = false;
                for (size_t j = 0; j < w.body.size(); ++j) {
                    const Stmt& st = *w.body[j];
                    if (isIncrement(st, iv)) { inc = static_cast<int>(j); continue; }
                    if (st.kind != StmtKind::Assign) { kind = -2; break; }
                    const Expr* sh = accumulateOperand(st, st.varIndex, ExprKind::Shr);
                    if (sh && isConstE(sh, 1) && shiftAt < 0 && accAt >= 0) {
                        shiftAt = static_cast<int>(j);
                        x = x < 0 ? st.varIndex : x;
                        if (st.varIndex != x) { kind = -2; break; }
                        continue;
                    }
                    if (accAt >= 0) { kind = -2; break; }
                    accAt = static_cast<int>(j);
                    acc = st.varIndex;
                    const Expr& e = *st.valueExpr;
                    const Expr* bit = nullptr;
                    if (e.kind == ExprKind::BitOr && e.lhs->kind == ExprKind::Shl && isVarE(e.lhs->lhs.get(), acc) &&
                        isConstE(e.lhs->rhs.get(), 1)) {
                        kind = 2;
                        bit = e.rhs.get();
                    } else if ((bit = accumulateOperand(st, acc, ExprKind::Add))) {
                        kind = 0;
                    } else if ((bit = accumulateOperand(st, acc, ExprKind::BitXor))) {
                        kind = 1;
                    }
                    // bit: (x >> i) & 1, or x & 1 with x = x >> 1 later in the body
                    if (!bit || bit->kind != ExprKind::BitAnd || !isConstE(bit->rhs.get(), 1)) { kind = -2; break; }
                    const Expr* src = bit->lhs.get();
                    if (src->kind == ExprKind::Shr && src->lhs->kind == ExprKind::Var && isVarE(src->rhs.get(), iv)) {
                        x = src->lhs->varIndex;
                    } else if (src->kind == ExprKind::Var) {
                        x = src->varIndex;
                        shifted = true;
                    } else {
                        kind = -2;
                        break;
                    }
                }
                if (kind < 0 || inc < 0 || accAt < 0 || x < 0 || x == acc || x == iv || acc == iv) continue;
                if (shifted != (shiftAt >= 0) || w.body.size() != static_cast<size_t>(2 + (shifted ? 1 : 0))) continue;
                if (shifted && inc < shiftAt) continue;
                int maskW = W == 32 ? -1 : static_cast<int>((1u << W) - 1u);
                out.push_back(makeAssign(t, W == 32 ? V(x) : B(ExprKind::BitAnd, V(x), K(maskW))));
                if (kind == 0) {
                    appendPopcount(out, t);
                    out.push_back(makeAssign(acc, B(ExprKind::Add, V(acc), V(t))));
                } else if (kind == 1) {
                    for (int k = 16; k >= 1; k /= 2) out.push_back(foldStep(t, ExprKind::BitXor, k));
                    out.push_back(makeAssign(acc, B(ExprKind::BitXor, V(acc), B(ExprKind::BitAnd, V(t), K(1)))));
                } else {
                    appendReverse32(out, t);
                    ExprPtr rev = W == 32 ? V(t) : B(ExprKind::BitAnd, B(ExprKind::Shr, V(t), K(32 - W)), K(maskW));
                    out.push_back(makeAssign(acc, W == 32 ? std::move(rev)
                                                          : B(ExprKind::BitOr, B(ExprKind::Shl, V(acc), K(W)), std::move(rev))));
                }
                if (shifted) out.push_back(makeAssign(x, B(ExprKind::Shr, V(x), K(W == 32 ? 31 : W))));
                out.push_back(makeAssign(iv, K(W)));
            } else {  // which == 10: p = 1; while (p < x) p = p << 1  ->  smear(x - 1) + 1
                if (t < 0 || c.kind != ExprKind::Lt || c.lhs->kind != ExprKind::Var || w.body.size() != 1) continue;
                int pv = c.lhs->varIndex;
                const Expr* xE = c.rhs.get();
                if (xE->kind != ExprKind::Var && xE->kind != ExprKind::IntConst) continue;
                if (isVarE(xE, pv) || !initialisedTo(*l, i, pv, 1)) continue;
                const Stmt& st = *w.body[0];
                if (st.kind != StmtKind::Assign || st.varIndex != pv) continue;
                const Expr& e = *st.valueExpr;
                bool dbl = (e.kind == ExprKind::Shl && isVarE(e.lhs.get(), pv) && isConstE(e.rhs.get(), 1)) ||
                           (e.kind == ExprKind::Mul && ((isVarE(e.lhs.get(), pv) && isConstE(e.rhs.get(), 2)) ||
                                                        (isVarE(e.rhs.get(), pv) && isConstE(e.lhs.get(), 2)))) ||
                           (e.kind == ExprKind::Add && isVarE(e.lhs.get(), pv) && isVarE(e.rhs.get(), pv));
                if (!dbl) continue;
                std::vector<StmtPtr> body;
                body.push_back(makeAssign(t, B(ExprKind::Sub, xE->clone(), K(1))));
                for (int k = 1; k <= 16; k *= 2) body.push_back(foldStep(t, ExprKind::BitOr, k));
                body.push_back(makeAssign(pv, B(ExprKind::Add, V(t), K(1))));
                out.push_back(makeIf(c.clone(), std::move(body)));
            }
            ok.push_back({l, i});
            repl.push_back(std::move(out));
        }
        if (ok.empty()) return false;
        size_t k = static_cast<size_t>(rng.uniformInt(0, static_cast<int>(ok.size()) - 1));
        auto [l, i] = ok[k];
        l->erase(l->begin() + static_cast<long>(i));
        l->insert(l->begin() + static_cast<long>(i), std::make_move_iterator(repl[k].begin()),
                  std::make_move_iterator(repl[k].end()));
        return true;
    }

    // Phase 3c (log 190): edits that change the algorithm's shape rather
    // than one node. Unrolling is exact; the others are checked by the tests
    // like any mutant.
    bool algorithmicOnce(Slots& s, const Stmt& root, int which = -1) {
        if (which < 0 && idiom && rng.chance(0.5)) which = rng.uniformInt(7, 11);
        if (which >= 7) {
            bool ok = idiomOnce(s, root, which);
            idiomApplied = idiomApplied || ok;
            return ok;
        }
        switch (which >= 0 ? which : rng.uniformInt(0, reach ? 6 : 4)) {
            case 0: {  // unroll a loop twice: while (c) { B; if (!c) break; B }
                auto w = sites(s, StmtKind::While);
                if (w.empty()) return false;
                auto [l, i] = w[static_cast<size_t>(rng.uniformInt(0, static_cast<int>(w.size()) - 1))];
                Stmt& loop = *(*l)[i];
                std::vector<StmtPtr> copy;
                for (const auto& b : loop.body) copy.push_back(b->clone());
                std::vector<StmtPtr> brk;
                brk.push_back(makeBreak());
                loop.body.push_back(
                    makeIf(makeBinOp(ExprKind::Eq, loop.cond->clone(), makeIntConst(0)), std::move(brk)));
                for (auto& b : copy) loop.body.push_back(std::move(b));
                return true;
            }
            case 1: {  // if (c) v = a; [else v = b;]  ->  v = b ^ ((a ^ b) & -c)
                std::vector<std::pair<std::vector<StmtPtr>*, size_t>> ok;
                for (auto [l, i] : sites(s, StmtKind::If)) {
                    const Stmt& st = *(*l)[i];
                    if (st.body.size() != 1 || st.body[0]->kind != StmtKind::Assign) continue;
                    if (st.elseBody.size() > 1) continue;
                    if (st.elseBody.size() == 1 && (st.elseBody[0]->kind != StmtKind::Assign ||
                                                    st.elseBody[0]->varIndex != st.body[0]->varIndex))
                        continue;
                    ok.push_back({l, i});
                }
                if (ok.empty()) return false;
                auto [l, i] = ok[static_cast<size_t>(rng.uniformInt(0, static_cast<int>(ok.size()) - 1))];
                Stmt& st = *(*l)[i];
                int v = st.body[0]->varIndex;
                ExprPtr t = isComparison(st.cond->kind)
                                ? st.cond->clone()
                                : makeBinOp(ExprKind::Ne, st.cond->clone(), makeIntConst(0));
                ExprPtr mask = makeBinOp(ExprKind::Sub, makeIntConst(0), std::move(t));
                ExprPtr a = st.body[0]->valueExpr->clone();
                ExprPtr b = st.elseBody.empty() ? makeVar(v) : st.elseBody[0]->valueExpr->clone();
                ExprPtr bb = b->clone();
                ExprPtr sel = makeBinOp(
                    ExprKind::BitXor, std::move(b),
                    makeBinOp(ExprKind::BitAnd, makeBinOp(ExprKind::BitXor, std::move(a), std::move(bb)), std::move(mask)));
                (*l)[i] = makeAssign(v, std::move(sel));
                return true;
            }
            case 2: {  // insert an early exit: `if (c) break;` in a loop, or `if (c) return E;`
                auto loops = sites(s, StmtKind::While);
                std::vector<StmtPtr> body;
                std::vector<StmtPtr>* target = nullptr;
                if (!loops.empty() && rng.chance(0.5)) {
                    auto [l, i] = loops[static_cast<size_t>(rng.uniformInt(0, static_cast<int>(loops.size()) - 1))];
                    target = &(*l)[i]->body;
                    body.push_back(makeBreak());
                } else {
                    target = pick(s.lists);
                    if (returnsValue) body.push_back(makeAssignArray(makeIntConst(0), expr(1)));
                    body.push_back(makeReturn());
                }
                target->insert(target->begin() + rng.uniformInt(0, static_cast<int>(target->size())),
                               makeIf(condition(), std::move(body)));
                return true;
            }
            case 3: {  // a loop that runs at most once: while (c) B -> if (c) B
                auto w = sites(s, StmtKind::While);
                if (w.empty()) return false;
                auto [l, i] = w[static_cast<size_t>(rng.uniformInt(0, static_cast<int>(w.size()) - 1))];
                StmtPtr loop = std::move((*l)[i]);
                (*l)[i] = makeIf(std::move(loop->cond), std::move(loop->body));
                return true;
            }
            case 5: {  // merge a constant branch: if (c) R = K; else R = E(x)  ->  if (c) x = a; R = E(x)
                std::vector<std::pair<std::vector<StmtPtr>*, size_t>> ok;
                for (auto [l, i] : sites(s, StmtKind::If))
                    if (constantBranch(*(*l)[i]) >= 0) ok.push_back({l, i});
                if (ok.empty()) return false;
                auto [l, i] = ok[static_cast<size_t>(rng.uniformInt(0, static_cast<int>(ok.size()) - 1))];
                StmtPtr st = std::move((*l)[i]);
                bool constThen = constantBranch(*st) == 0;
                std::vector<StmtPtr>& kb = constThen ? st->body : st->elseBody;
                std::vector<StmtPtr>& eb = constThen ? st->elseBody : st->body;
                std::set<int> vars;
                collectVars(eb[0]->valueExpr.get(), vars);
                if (vars.empty()) {
                    (*l)[i] = std::move(st);
                    return false;
                }
                std::vector<int> vs(vars.begin(), vars.end());
                int x = pick(vs);
                // a: the constant result itself, or a constant of the condition +-1
                // (memsys3Roundup: n <= 12 ? 12 : E(n), and E(12) == 12).
                std::set<long long> cs;
                collectConsts(st->cond.get(), cs);
                std::vector<int> as{kb[0]->valueExpr->intValue};
                for (long long c : cs)
                    for (long long d = -1; d <= 1; ++d) as.push_back(static_cast<int>(c + d));
                int a = pick(as);
                ExprPtr cond = constThen ? std::move(st->cond)
                                         : makeBinOp(ExprKind::Eq, std::move(st->cond), makeIntConst(0));
                std::vector<StmtPtr> setX;
                setX.push_back(makeAssign(x, makeIntConst(a)));
                std::vector<StmtPtr> out;
                out.push_back(makeIf(std::move(cond), std::move(setX)));
                for (auto& b : eb) out.push_back(std::move(b));
                l->erase(l->begin() + static_cast<long>(i));
                l->insert(l->begin() + static_cast<long>(i), std::make_move_iterator(out.begin()),
                          std::make_move_iterator(out.end()));
                return true;
            }
            case 6: {  // split a divisibility test: x % (2^p * b) == 0  ->  (x % 2^p == 0) & (x % b == 0)
                std::vector<ExprPtr*> ok;
                for (auto* e : s.exprs)
                    if (splittableModTest(**e)) ok.push_back(e);
                if (ok.empty()) return false;
                ExprPtr& e = *pick(ok);
                bool modLeft = e->lhs->kind == ExprKind::Mod;
                const Expr& mod = modLeft ? *e->lhs : *e->rhs;
                int c = mod.rhs->intValue, a = 1;
                while (c % (2 * a) == 0) a *= 2;
                ExprKind k = e->kind;
                auto test = [&](int m) {
                    return makeBinOp(k, makeBinOp(ExprKind::Mod, mod.lhs->clone(), makeIntConst(m)), makeIntConst(0));
                };
                e = makeBinOp(k == ExprKind::Eq ? ExprKind::BitAnd : ExprKind::BitOr, test(a), test(c / a));
                return true;
            }
            default: {  // strength reduction by a power of two: * -> <<, / -> >>, % -> &
                std::vector<ExprPtr*> red;
                for (auto* e : s.exprs) {
                    const Expr& x = **e;
                    if ((x.kind == ExprKind::Mul || x.kind == ExprKind::Div || x.kind == ExprKind::Mod) &&
                        x.rhs->kind == ExprKind::IntConst && x.rhs->intValue > 1 &&
                        (x.rhs->intValue & (x.rhs->intValue - 1)) == 0)
                        red.push_back(e);
                }
                if (red.empty()) return false;
                Expr& x = **pick(red);
                int k = x.rhs->intValue, p = 0;
                while ((1 << p) < k) ++p;
                if (x.kind == ExprKind::Mul) {
                    x.kind = ExprKind::Shl;
                    x.rhs->intValue = p;
                } else if (x.kind == ExprKind::Div) {
                    x.kind = ExprKind::Shr;
                    x.rhs->intValue = p;
                } else {
                    x.kind = ExprKind::BitAnd;
                    x.rhs->intValue = k - 1;
                }
                return true;
            }
        }
    }

    // Applies one edit; false when the chosen edit had nothing to act on.
    bool once(Stmt& root) {
        Slots s;
        collect(root, s);
        if (algorithmic && rng.chance(0.25)) {
            bool ok = algorithmicOnce(s, root);
            structural = structural || ok;
            return ok;
        }
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

// Constants of the reference, with constant subexpressions folded
// (`INT_MAX - 1900` is one constant).
std::optional<long long> foldConst(const Expr& e) {
    if (e.kind == ExprKind::IntConst) return e.intValue;
    if (!e.lhs || !e.rhs) return std::nullopt;
    auto a = foldConst(*e.lhs), b = foldConst(*e.rhs);
    if (!a || !b) return std::nullopt;
    auto wrap = [](long long v) { return static_cast<long long>(static_cast<int>(static_cast<uint32_t>(v))); };
    switch (e.kind) {
        case ExprKind::Add: return wrap(*a + *b);
        case ExprKind::Sub: return wrap(*a - *b);
        case ExprKind::Mul: return wrap(*a * *b);
        default: return std::nullopt;
    }
}

void collectConsts(const Expr* e, std::set<long long>& out) {
    if (!e) return;
    if (auto c = foldConst(*e)) {
        out.insert(*c);
        return;
    }
    collectConsts(e->lhs.get(), out);
    collectConsts(e->rhs.get(), out);
}

void collectConsts(const Stmt& s, std::set<long long>& out) {
    for (const Expr* e : {s.valueExpr.get(), s.indexExpr.get(), s.indexExpr2.get(), s.cond.get()}) collectConsts(e, out);
    for (const auto& b : s.body) collectConsts(*b, out);
    for (const auto& b : s.elseBody) collectConsts(*b, out);
}

// Section 192: inputs at the reference's constants (c - 2 .. c + 2 in each
// argument) and, for a constant within 4096 of an int limit, the whole tail
// up to that limit. musl's is_leap guards `y > INT_MAX - 1900`; random and
// grid inputs never reach that region, and candidates without the guard
// passed every test.
// Log 204: constants c, c +- 1..2, sums and differences of two constants
// (+- 1), and 0, +-1, INT_MIN, INT_MAX: where a multi-argument expression's
// comparisons change.
std::vector<int> derivedConstants(const std::set<long long>& consts) {
    std::set<long long> v = {0, 1, -1, INT_MIN, INT_MAX};
    for (long long c : consts)
        for (long long d = -2; d <= 2; ++d) v.insert(c + d);
    for (long long a : consts)
        for (long long b : consts)
            for (long long d = -1; d <= 1; ++d) {
                v.insert(a + b + d);
                v.insert(a - b + d);
            }
    std::vector<int> out;
    for (long long x : v)
        if (x >= INT_MIN && x <= INT_MAX) out.push_back(static_cast<int>(x));
    return out;
}

std::vector<std::vector<int>> boundaryInputs(const Stmt& reference, const CFunctionAbi& abi) {
    std::vector<std::vector<int>> res;
    if (abi.kind != CFunctionAbi::Kind::Scalars) return res;
    std::set<long long> consts;
    collectConsts(reference, consts);
    std::set<long long> values;
    for (long long c : consts) {
        for (long long d = -2; d <= 2; ++d) values.insert(c + d);
        if (INT_MAX - c <= 4096)
            for (long long v = c; v <= INT_MAX; ++v) values.insert(v);
        if (c - static_cast<long long>(INT_MIN) <= 4096)
            for (long long v = INT_MIN; v <= c; ++v) values.insert(v);
    }
    size_t arity = abi.params.size();
    for (long long v : values) {
        if (v < INT_MIN || v > INT_MAX) continue;
        for (size_t k = 0; k < arity && res.size() < 50000; ++k)
            for (int other : {0, 1, -1}) {
                std::vector<int> in(arity, other);
                in[k] = static_cast<int>(v);
                res.push_back(in);
                if (arity == 1) break;
            }
    }
    // Log 204: two arguments at derived constants together. `ex + 120 < ey ||
    // ey == 0x7fff` minus its `||` term is wrong only at ey = 0x7fff with
    // ex >= 0x7fff - 120: no single-argument boundary and no random pair finds it.
    if (arity >= 2) {
        std::vector<int> d = derivedConstants(consts);
        for (size_t i = 0; i < arity; ++i)
            for (size_t j = i + 1; j < arity; ++j)
                for (int a : d)
                    for (int b : d) {
                        if (res.size() >= 120000) return res;
                        std::vector<int> in(arity, 0);
                        in[i] = a;
                        in[j] = b;
                        res.push_back(in);
                    }
    }
    return res;
}

// One in-domain-looking neighbour of a workload input (log 199c): the
// workload covers a thin slice of the domain (text) and random arrays miss it
// (no NUL), so CEGIS and the final check mutate workload inputs. 1-3 edits:
// a random value in the workload's range, one bit flipped, an element set to
// another one with one bit flipped or +-1 (near-equal aligned pairs, e.g.
// "[" vs "{"), a copy, a zero (a shorter string) or a program constant +-2.
// Inputs the reference rejects are dropped by the caller.
std::vector<int> mutateInput(const std::vector<int>& seed, Random& r, int lo, int hi,
                             const std::vector<int>& consts) {
    std::vector<int> in = seed;
    if (in.empty()) return in;
    int last = static_cast<int>(in.size()) - 1;
    int bits = 1;
    while (bits < 31 && (1LL << bits) <= std::max(std::abs(static_cast<long long>(lo)), static_cast<long long>(hi))) ++bits;
    int edits = r.uniformInt(1, 3);
    for (int e = 0; e < edits; ++e) {
        size_t i = static_cast<size_t>(r.uniformInt(0, last));
        size_t j = static_cast<size_t>(r.uniformInt(0, last));
        switch (r.uniformInt(0, 5)) {
        case 0: in[i] = r.uniformInt(lo, hi); break;
        case 1: in[i] ^= 1 << r.uniformInt(0, bits - 1); break;
        case 2: in[j] = r.uniformInt(0, 1) ? in[i] ^ (1 << r.uniformInt(0, bits - 1)) : in[i] + (r.uniformInt(0, 1) ? 1 : -1); break;
        case 3: in[i] = in[j]; break;
        case 4: in[i] = 0; break;
        default:
            if (!consts.empty()) in[i] = consts[static_cast<size_t>(r.uniformInt(0, static_cast<int>(consts.size()) - 1))];
            break;
        }
    }
    return in;
}

// True when no operation of the program can fail at run time (division by a
// variable, shift by a variable, array index other than a constant argument
// slot): then the C function is total and comparing all arguments natively
// is a proof for a unary function.
bool cannotFail(const Expr* e, int arity) {
    if (!e) return true;
    if (e->kind == ExprKind::Div || e->kind == ExprKind::Mod) {
        auto d = foldConst(*e->rhs);
        if (!d || *d == 0 || *d == -1) return false;
    }
    if (e->kind == ExprKind::Shl || e->kind == ExprKind::Shr) {
        auto d = foldConst(*e->rhs);
        if (!d || *d < 0 || *d > 31) return false;
    }
    if (e->kind == ExprKind::ArrayAccess || e->kind == ExprKind::AuxAccess) {
        auto i = foldConst(*e->lhs);
        if (e->kind == ExprKind::AuxAccess || !i || *i < 0 || *i >= arity) return false;
    }
    return cannotFail(e->lhs.get(), arity) && cannotFail(e->rhs.get(), arity);
}

bool cannotFail(const Stmt& s, int arity) {
    if (s.kind == StmtKind::AssignArray) {
        auto i = s.indexExpr ? foldConst(*s.indexExpr) : std::nullopt;
        if (!i || *i < 0 || *i >= arity) return false;
    }
    for (const Expr* e : {s.valueExpr.get(), s.indexExpr.get(), s.indexExpr2.get(), s.cond.get()})
        if (!cannotFail(e, arity)) return false;
    for (const auto& b : s.body)
        if (!cannotFail(*b, arity)) return false;
    for (const auto& b : s.elseBody)
        if (!cannotFail(*b, arity)) return false;
    return true;
}

struct Candidate {
    Program program;
    std::string key;
    long long cost = 0;
    int fails = 0;
    int size = 0;
    bool fromTimed = false;  // a mutant of a timed, measured-fast parent
    bool structural = false; // made by a phase 3c move (log 203)
    bool idiom = false;      // made by an idiom rewrite (log 208)
};

struct Timed {
    Program program;
    std::string key;
    double speedup = 0;
    long long cost = 0;
};

}  // namespace

Program mutateForSuperopt(const Stmt& program, int numVars, int arity, uint64_t seed, bool algorithmic,
                          bool returnsValue, bool reach, bool* structural, bool idiom, bool* idiomApplied) {
    Mutator m{Random(seed), numVars, arity, algorithmic, returnsValue, reach, idiom};
    Program p = program.clone();
    int edits = 1;
    while (edits < 4 && m.rng.chance(0.35)) ++edits;
    for (int i = 0; i < edits; ++i)
        for (int tries = 0; tries < 8 && !m.once(*p); ++tries) {
        }
    if (structural) *structural = m.structural;
    if (idiomApplied) *idiomApplied = m.idiomApplied;
    return p;
}

Program applyAlgorithmicMove(const Stmt& program, int move, int numVars, int arity, uint64_t seed,
                             bool returnsValue) {
    Mutator m{Random(seed), numVars, arity, true, returnsValue, true, true};
    Program p = program.clone();
    Slots s;
    collect(*p, s);
    if (!m.algorithmicOnce(s, *p, move)) return nullptr;
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

    // The user's workload: in-domain inputs only, each also a test.
    for (const auto& in : config_.userWorkload) {
        auto want = problem_.expected(in);
        if (!want) continue;
        userWorkload_.push_back(in);
        if (userWorkload_.size() <= 64) tests_.push_back({in, *want});
    }
    if (!config_.userWorkload.empty() && userWorkload_.empty())
        throw CImportError("--workload: the reference fails or times out on every input");

    // Workload for the cost: realistic sizes, not the small test levels (or
    // an even sample of the user's workload).
    Random rng(config_.seed ^ 0x51ed270b27f0a3c5ULL);
    size_t costSize = scalars ? 64u : 24u;
    for (size_t i = 0; !userWorkload_.empty() && i < costSize; ++i)
        workload_.push_back(userWorkload_[i * userWorkload_.size() / costSize]);
    // Log 204: bounded. A function defined on a sliver of the inputs (a shift
    // by `150 - e`, defined for 32 values of e) drew random inputs forever;
    // the rest is filled from the in-domain tests.
    for (long tries = 0; workload_.size() < costSize && tries < 200000; ++tries) {
        std::vector<int> in;
        if (scalars) {
            in = randomInput(rng, abi, 0);
        } else {
            in.resize(static_cast<size_t>(rng.uniformInt(16, 64)));
            for (auto& x : in) x = rng.uniformInt(-50, 50);
        }
        if (problem_.expected(in)) workload_.push_back(in);
    }
    for (size_t i = 0; workload_.size() < costSize && !tests_.empty(); ++i)
        workload_.push_back(tests_[(i * 7919) % tests_.size()].input);
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
    result.userWorkload = static_cast<int>(userWorkload_.size());
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

    const std::vector<std::vector<int>> boundary = boundaryInputs(problem_.referenceProgram(), abi);
    // Mutation fuzz (log 199c): the workload's value range, and the constants
    // (+-2) of the reference and the candidate.
    int wlLo = 0, wlHi = 0;
    for (const auto& in : userWorkload_)
        for (int v : in) { wlLo = std::min(wlLo, v); wlHi = std::max(wlHi, v); }
    auto mutationConsts = [&](const Stmt& p) {
        std::set<long long> cs, vs;
        collectConsts(problem_.referenceProgram(), cs);
        collectConsts(p, cs);
        for (long long c : cs)
            for (long long d = -2; d <= 2; ++d)
                if (c + d >= INT_MIN && c + d <= INT_MAX) vs.insert(c + d);
        return std::vector<int>(vs.begin(), vs.end());
    };
    // The final check of an array winner: 200,000 mutations, fixed seed.
    auto fuzzRefutes = [&](const Stmt& p) {
        Random r(config_.seed ^ 0x6a09e667f3bcc909ULL);
        std::vector<int> pc = mutationConsts(p), o;
        for (int i = 0; i < 200000; ++i) {
            const auto& seed = userWorkload_[static_cast<size_t>(r.uniformInt(0, static_cast<int>(userWorkload_.size()) - 1))];
            std::vector<int> in = mutateInput(seed, r, wlLo, wlHi, pc);
            long steps = problem_.referenceSteps(in);
            if (steps >= 0 && !agrees(p, in, stepBudget(steps), o)) return true;
        }
        return false;
    };
    // Log 204: the final check of a winner with several scalar arguments (no
    // exhaustive proof): 1,000,000 inputs, each argument random, a derived
    // constant of the reference and the candidate, one plus a small offset, or
    // a small value.
    auto scalarFuzzRefutes = [&](const Stmt& p) {
        std::set<long long> cs;
        collectConsts(problem_.referenceProgram(), cs);
        collectConsts(p, cs);
        std::vector<int> d = derivedConstants(cs), o;
        Random r(config_.seed ^ 0xbb67ae8584caa73bULL);
        std::vector<int> in(abi.params.size());
        for (int i = 0; i < 1000000; ++i) {
            for (auto& x : in) {
                int pick = d[static_cast<size_t>(r.uniformInt(0, static_cast<int>(d.size()) - 1))];
                switch (r.uniformInt(0, 3)) {
                    case 0: x = static_cast<int>(r.engine()()); break;
                    case 1: x = pick; break;
                    case 2: x = static_cast<int>(static_cast<unsigned>(pick) + static_cast<unsigned>(r.uniformInt(-300, 300))); break;
                    default: x = r.uniformInt(-1000, 1000); break;
                }
            }
            long steps = problem_.referenceSteps(in);
            if (steps >= 0 && !agrees(p, in, stepBudget(steps), o)) return true;
        }
        return false;
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
        for (const auto& in : boundary)
            if (bad(in)) return in;
        // The whole user workload (log 199): random arrays rarely hold a NUL,
        // so for string code they are almost all outside the domain, and a
        // candidate wrong on 7% of real strings passed every other check.
        for (const auto& in : userWorkload_)
            if (bad(in)) return in;
        Random r(rng.engine()());
        if (abi.kind == CFunctionAbi::Kind::Array && !userWorkload_.empty()) {
            std::vector<int> pc = mutationConsts(p);
            for (int i = 0; i < 3000; ++i) {
                const auto& seed = userWorkload_[static_cast<size_t>(r.uniformInt(0, static_cast<int>(userWorkload_.size()) - 1))];
                std::vector<int> in = mutateInput(seed, r, wlLo, wlHi, pc);
                if (bad(in)) return in;
            }
        }
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
    std::unique_ptr<CompiledTimer> timer, confirmTimer, altConfirmTimer;
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
        // Section 190: the compiled workload is its own, larger set. With the 24
        // cost inputs repeated, the branch predictor memorizes their branches and
        // a branchy candidate times faster than it runs on fresh data.
        //
        // Log 197: the search times hundreds of candidates on one fixed input
        // sequence, and the fastest of them can be fast on that sequence only
        // (a branch the predictor learned on it: 1.53x there, 0.57x on other
        // inputs of the same kind). The final winners are therefore confirmed
        // on inputs the search never timed: the other half of the user's
        // workload (alternate inputs), or a second random set.
        //
        // Log 198: each set is one long pass with no repeated sequence (65,536
        // scalar inputs): repeating 512 inputs let the predictor learn the
        // branches of branchy code. A user workload is split into its even and
        // odd inputs, and each half is resampled in a random order to the full
        // size, so the predictor sees the workload's distribution, not a short
        // sequence it can learn.
        std::vector<std::vector<int>> timing, confirm;
        bool scalarAbi = abi.kind == CFunctionAbi::Kind::Scalars;
        const size_t cap = static_cast<size_t>(scalarAbi ? config_.timingInputs
                                                         : std::min(config_.timingInputs, 16384));
        if (!userWorkload_.empty()) {
            std::vector<std::vector<int>> even, odd;
            for (size_t i = 0; i < userWorkload_.size(); ++i)
                (i % 2 == 0 || userWorkload_.size() == 1 ? even : odd).push_back(userWorkload_[i]);
            if (odd.empty()) odd = even;
            auto resample = [&](uint64_t salt, const std::vector<std::vector<int>>& from,
                                std::vector<std::vector<int>>& set) {
                Random wrng(config_.seed ^ salt);
                for (size_t i = 0; i < cap; ++i)
                    set.push_back(from[static_cast<size_t>(wrng.uniformInt(0, static_cast<int>(from.size()) - 1))]);
            };
            resample(0x2545f4914f6cdd1dULL, even, timing);
            resample(0x9e3779b97f4a7c15ULL, odd, confirm);
        }
        auto randomSet = [&](uint64_t salt, std::vector<std::vector<int>>& set) {
            Random trng(config_.seed ^ salt);
            for (size_t tries = 0; set.size() < cap && tries < 20 * cap; ++tries) {
                std::vector<int> in;
                if (scalarAbi) {
                    in = randomInput(trng, abi, 0);
                } else {
                    in.resize(static_cast<size_t>(trng.uniformInt(16, 64)));
                    for (auto& x : in) x = trng.uniformInt(-50, 50);
                }
                if (problem_.expected(in)) set.push_back(std::move(in));
            }
            // Log 204: too few random inputs in the domain: resample the tests.
            for (size_t i = 0; set.size() < cap && !tests_.empty(); ++i)
                set.push_back(tests_[static_cast<size_t>(trng.uniformInt(0, static_cast<int>(tests_.size()) - 1))].input);
        };
        if (userWorkload_.empty()) {
            randomSet(0x2545f4914f6cdd1dULL, timing);
            randomSet(0x9e3779b97f4a7c15ULL, confirm);
        }
        // Log 201: a unary function whose timing inputs are all bytes is timed in
        // a loop over a byte buffer (unsigned when they fit 0..255).
        std::string byteType, byteEligible;
        if (scalarAbi && abi.params.size() == 1) {
            bool u8 = true, s8 = true;
            for (const auto* set : {&timing, &confirm})
                for (const auto& in : *set) {
                    u8 = u8 && in[0] >= 0 && in[0] <= 255;
                    s8 = s8 && in[0] >= -128 && in[0] <= 127;
                }
            byteEligible = u8 ? "unsigned char" : s8 ? "signed char" : "";
            if (config_.timingContext == "byte") byteType = byteEligible;
        }
        result.timingContext = byteType.empty() ? "int" : byteType == "unsigned char" ? "uchar" : "schar";
        timer = std::make_unique<CompiledTimer>(config_.originalSource, abi, timing, config_.workDir, byteType,
                                                config_.programTimer);
        confirmTimer = std::make_unique<CompiledTimer>(config_.originalSource, abi, confirm, config_.workDir + "/confirm",
                                                       byteType, config_.programTimer);
        if (config_.crossContextConfirm && !byteEligible.empty())
            altConfirmTimer = std::make_unique<CompiledTimer>(config_.originalSource, abi, confirm,
                                                              config_.workDir + "/confirm_alt",
                                                              byteType.empty() ? byteEligible : "");
        // Noise: the original against itself, on both input sets.
        double spread = 0;
        auto selfControl = [&](CompiledTimer& tm) {
            std::vector<double> self;
            for (int i = 0; i < 3; ++i) {
                CompiledTimer::Timing t = tm.time(config_.originalSource);
                if (!t.ok) throw CImportError("compiled timing failed: " + t.error);
                self.push_back(t.speedup);
                spread = std::max(spread, std::abs(1.0 - t.speedup));
            }
            std::sort(self.begin(), self.end());
            return self[1];
        };
        result.selfControl = selfControl(*timer);
        result.confirmSelfControl = selfControl(*confirmTimer);
        if (altConfirmTimer) {
            double keep = spread;
            result.crossContextSelfControl = selfControl(*altConfirmTimer);
            spread = keep;  // the noise threshold stays that of the timing context
        }
        result.noiseThreshold = std::max(0.03, 2.0 * spread);
        if (auto c = emit(problem_.referenceProgram())) {
            CompiledTimer::Timing t = timer->time(*c);
            if (t.ok) result.emittedReference = t.speedup;
        }
        result.timings = 7;
    }
    // Times the candidates pool[picks] (correct ones only) into `timed`.
    auto timePicks = [&](const std::vector<Candidate>& pool, const std::vector<size_t>& picks) {
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
        timePicks(pool, picks);
    };
    // Log 203 (reach moves): the model does not see branch mispredictions, so
    // a correct structural rewrite (e.g. a clamp in place of a branch) can
    // cost more than the beam's equivalents and drop out before it is timed.
    // One such candidate per generation is timed before the beam is cut.
    auto timeStructural = [&](std::vector<Candidate>& pool) {
        if (!timer || !config_.reachMoves) return;
        if (result.timingSeconds > config_.timeFraction * std::max(1.0, elapsed())) return;
        std::vector<size_t> cands;
        for (size_t i = 0; i < pool.size(); ++i)
            if (pool[i].structural && !timedKeys.count(pool[i].key)) cands.push_back(i);
        if (cands.empty()) return;
        std::vector<size_t> picks{cands[static_cast<size_t>(rng.uniformInt(0, static_cast<int>(cands.size()) - 1))]};
        timePicks(pool, picks);
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
                c.program = mutateForSuperopt(*parent->program, numVars, arity, rng.engine()(),
                                              config_.algorithmicMoves, abi.returnsValue, config_.reachMoves,
                                              &c.structural, config_.idiomMoves, &c.idiom);
                c.fromTimed = parent->fromTimed;
                c.size = c.program->size();
                // Log 208: an idiom rewrite (a SWAR block) may outgrow the size
                // cap meant for local edits; with idiom moves on, the cap also
                // follows the parent, so such a rewrite can still be refined.
                int cap = config_.idiomMoves ? std::max(maxSize, parent->size + 20) : maxSize;
                if (c.idiom) cap = std::max(cap, c.size);
                if (c.size > cap) continue;
                c.key = printStmt(*c.program);
                if (!seen.insert(c.key).second) continue;
                ++result.evaluated;
                c.fails = failures(*c.program, failCap);
                if (c.fails == 0) {
                    c.cost = cost(*c.program);
                    if (c.cost < 0) continue;
                    ++result.correct;
                    if (c.idiom) ++result.idiomCorrect;
                    fresh.push_back(std::move(c));
                } else if (c.fails <= failCap) {
                    freshExplore.push_back(std::move(c));
                }
            }
        }
        timeStructural(fresh);
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
        // Re-measure the fastest few (3 runs of 21 trials each, median) on the
        // confirmation inputs and keep one only if it beats the noise
        // threshold there; otherwise the answer is "no measured gain": the
        // reference.
        result.best = problem_.referenceProgram().clone();
        result.bestCost = result.referenceCost;
        result.measuredSpeedup = 1.0;
        // Every unary scalar function gets the compiled check on all 2^32
        // arguments (log 194: checkint's reference can fail statically, a
        // variable shift, and a wrong winner went through as `tested`). Where
        // the reference itself fails (undefined in C), the compiled values
        // are arbitrary, so a mismatch there does not refute the candidate.
        const bool exhaustiveOk = abi.kind == CFunctionAbi::Kind::Scalars && abi.params.size() == 1;
        const bool referenceTotal = cannotFail(problem_.referenceProgram(), 1);
        for (size_t k = 0; k < timed.size() && k < 3; ++k) {
            if (timed[k].speedup <= 1.0 + result.noiseThreshold) break;
            auto src = emit(*timed[k].program);
            std::vector<double> r;
            for (int rep = 0; rep < 3 && src; ++rep) {
                CompiledTimer::Timing t = confirmTimer->time(*src, 21);
                ++result.timings;
                if (t.ok) r.push_back(t.speedup);
            }
            if (r.size() < 3) continue;
            std::sort(r.begin(), r.end());
            double alt = 0;
            if (altConfirmTimer && src && r[1] > 1.0 + result.noiseThreshold && r[1] > result.measuredSpeedup) {
                std::vector<double> a;
                for (int rep = 0; rep < 3; ++rep) {
                    CompiledTimer::Timing t = altConfirmTimer->time(*src, 21);
                    ++result.timings;
                    if (t.ok) a.push_back(t.speedup);
                }
                if (a.size() < 3) continue;
                std::sort(a.begin(), a.end());
                alt = a[1];
                if (alt <= 1.0 + result.noiseThreshold) {
                    ++result.crossContextRejected;
                    if (config_.verbose)
                        std::cerr << "cross-context check rejects a timed winner: " << r[1] << " vs " << alt << "\n";
                    continue;
                }
                r[1] = std::min(r[1], alt);
            }
            if (r[1] > 1.0 + result.noiseThreshold && r[1] > result.measuredSpeedup) {
                if (exhaustiveOk && src) {
                    CompiledTimer::Exhaustive x = timer->exhaustive(*src);
                    bool refuted = x.complete && x.mismatches > 0;
                    if (refuted && !referenceTotal && x.mismatches <= CompiledTimer::maxListed) {
                        refuted = false;
                        for (int v : x.bad)
                            if (problem_.expected({v}).has_value()) {
                                refuted = true;
                                x.firstBad = v;
                                break;
                            }
                    }
                    if (refuted) {
                        ++result.exhaustiveRejected;
                        if (config_.verbose)
                            std::cerr << "exhaustive check refutes a timed winner: " << x.mismatches
                                      << " mismatches, first at " << x.firstBad << "\n";
                        continue;
                    }
                    result.equivalence = x.complete ? "exhaustive" : "tested";
                } else if (abi.kind == CFunctionAbi::Kind::Array && !userWorkload_.empty()) {
                    if (fuzzRefutes(*timed[k].program)) {
                        ++result.fuzzRejected;
                        if (config_.verbose) std::cerr << "mutation fuzz refutes a timed winner\n";
                        continue;
                    }
                    result.equivalence = "fuzzed";
                } else if (abi.kind == CFunctionAbi::Kind::Scalars && abi.params.size() >= 2) {
                    if (scalarFuzzRefutes(*timed[k].program)) {
                        ++result.fuzzRejected;
                        if (config_.verbose) std::cerr << "scalar fuzz refutes a timed winner\n";
                        continue;
                    }
                    result.equivalence = "fuzzed";
                } else {
                    result.equivalence = "tested";
                }
                result.best = timed[k].program->clone();
                result.bestCost = timed[k].cost;
                result.measuredSpeedup = r[1];
                result.crossContextSpeedup = alt;
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
