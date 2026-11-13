#pragma once

#include <array>

#include "ast/Expr.hpp"
#include "ast/Stmt.hpp"

namespace algosearch {

// Weighted dynamic cost of a run (superoptimizer prototype, docs/superopt.md
// phase 3): every executed node adds its kind's weight, and every check of a
// while condition adds `loopCheck` (the backward branch). A rough model of
// the machine code gcc -O2 produces, NOT interpreter steps: variables live in
// registers (free), a load or store costs 1, simple ALU ops 1, multiply 3,
// divide/modulo 20, a taken-or-not branch 1. Uncalibrated defaults; the
// measure that counts is the compiled speedup (scripts/c_speedup.py).
struct CostModel {
    std::array<int, 32> expr{};
    std::array<int, 16> stmt{};
    int loopCheck = 1;
    // Scalar-argument ABI: arr is a local array of the arguments, which gcc
    // keeps in registers, so arr[<constant>] costs nothing.
    bool constIndexFree = false;

    int weight(const Expr& e) const {
        if (constIndexFree && e.kind == ExprKind::ArrayAccess && e.lhs->kind == ExprKind::IntConst) return 0;
        return expr[static_cast<size_t>(e.kind)];
    }
    int weight(const Stmt& s) const {
        if (constIndexFree && s.kind == StmtKind::AssignArray && s.indexExpr->kind == ExprKind::IntConst) return 0;
        return stmt[static_cast<size_t>(s.kind)];
    }

    static CostModel x86Default(bool scalarAbi) {
        CostModel m;
        for (auto k : {ExprKind::Add, ExprKind::Sub, ExprKind::Lt, ExprKind::Gt, ExprKind::Eq, ExprKind::Le,
                       ExprKind::Ge, ExprKind::Ne, ExprKind::BitAnd, ExprKind::BitOr, ExprKind::BitXor, ExprKind::Shl,
                       ExprKind::Shr, ExprKind::ArrayAccess, ExprKind::AuxAccess})
            m.expr[static_cast<size_t>(k)] = 1;
        m.expr[static_cast<size_t>(ExprKind::Mul)] = 3;
        m.expr[static_cast<size_t>(ExprKind::Div)] = 20;
        m.expr[static_cast<size_t>(ExprKind::Mod)] = 20;
        m.stmt[static_cast<size_t>(StmtKind::AssignArray)] = 1;
        m.stmt[static_cast<size_t>(StmtKind::AssignAux)] = 1;
        m.stmt[static_cast<size_t>(StmtKind::If)] = 1;
        m.stmt[static_cast<size_t>(StmtKind::Swap)] = 4;
        m.stmt[static_cast<size_t>(StmtKind::Call)] = 5;
        m.constIndexFree = scalarAbi;
        return m;
    }
};

}  // namespace algosearch
