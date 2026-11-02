#include "ast/Expr.hpp"
#include "ast/Stmt.hpp"
#include "core/Random.hpp"
#include "interpreter/Interpreter.hpp"
#include "problems/FreshProblem.hpp"
#include "search/ProgramGenerator.hpp"
#include "test_framework.hpp"

using namespace algosearch;

TEST(fresh_references_match_hand_computed_values) {
    using K = FreshProblem::Kind;
    CHECK_EQ(FreshProblem::reference(K::SumMultiples3Or5, {10})[0], 23);
    CHECK_EQ(FreshProblem::reference(K::SumMultiples3Or5, {1})[0], 0);
    CHECK_EQ(FreshProblem::reference(K::Binomial, {5, 2})[0], 10);
    CHECK_EQ(FreshProblem::reference(K::Binomial, {12, 6})[0], 924);
    CHECK_EQ(FreshProblem::reference(K::Binomial, {7, 0})[0], 1);
    CHECK_EQ(FreshProblem::reference(K::DigitSum, {99999})[0], 45);
    CHECK_EQ(FreshProblem::reference(K::DigitSum, {1000})[0], 1);
    auto cm = FreshProblem::reference(K::CumulativeMax, {3, 1, 5, 2});
    CHECK_EQ(cm[1], 3);
    CHECK_EQ(cm[2], 5);
    CHECK_EQ(cm[3], 5);
    CHECK_EQ(FreshProblem::reference(K::Popcount, {1023})[0], 10);
    CHECK_EQ(FreshProblem::reference(K::Popcount, {0})[0], 0);
    CHECK_EQ(FreshProblem::reference(K::AlternatingSum, {5, 3, 4})[0], 6);
    CHECK_EQ(FreshProblem::reference(K::IsPowerOfTwo, {512})[0], 1);
    CHECK_EQ(FreshProblem::reference(K::IsPowerOfTwo, {1})[0], 1);
    CHECK_EQ(FreshProblem::reference(K::IsPowerOfTwo, {6})[0], 0);
    CHECK_EQ(FreshProblem::reference(K::BalancedParentheses, {1, 1, -1, -1})[0], 1);
    CHECK_EQ(FreshProblem::reference(K::BalancedParentheses, {1, -1, -1, 1})[0], 0);
    CHECK_EQ(FreshProblem::reference(K::BalancedParentheses, {1, 1, -1})[0], 0);
    auto sw = FreshProblem::reference(K::SwapAdjacentPairs, {1, 2, 3});
    CHECK_EQ(sw[0], 2);
    CHECK_EQ(sw[1], 1);
    CHECK_EQ(sw[2], 3);
    CHECK_EQ(FreshProblem::reference(K::CountPairsWithSum, {5, 1, 4, 2, 3})[0], 2);
}

TEST(fresh_problem_builds_train_and_test_sets_that_its_references_solve) {
    using K = FreshProblem::Kind;
    for (K k : {K::SumMultiples3Or5, K::Binomial, K::DigitSum, K::CumulativeMax, K::Popcount, K::AlternatingSum,
                K::IsPowerOfTwo, K::BalancedParentheses, K::SwapAdjacentPairs, K::CountPairsWithSum}) {
        FreshProblem p(k, 7, 1);
        CHECK_EQ(p.generateInstances(1).size(), static_cast<size_t>(200));
        CHECK_EQ(p.generateAdversarialInstances(1).size(), static_cast<size_t>(2000));
        for (const Instance& inst : p.generateInstances(1)) CHECK(p.isSolution(inst.input, inst.expectedOutput));
        for (const Instance& inst : p.generateAdversarialInstances(1)) CHECK(p.isSolution(inst.input, inst.expectedOutput));
    }
}

TEST(fresh_balanced_and_power_cases_are_about_half_positive) {
    using K = FreshProblem::Kind;
    for (K k : {K::BalancedParentheses, K::IsPowerOfTwo}) {
        FreshProblem p(k, 7, 3);
        int positives = 0;
        for (const Instance& inst : p.generateAdversarialInstances(1)) positives += inst.expectedOutput[0] == 1;
        CHECK(positives > 800 && positives < 1400);
    }
}

TEST(fresh_balanced_generator_only_emits_plus_minus_one_of_bounded_length) {
    FreshProblem p(FreshProblem::Kind::BalancedParentheses, 7, 5);
    for (const Instance& inst : p.generateAdversarialInstances(1)) {
        CHECK(!inst.input.empty() && inst.input.size() <= 10);
        for (int x : inst.input) CHECK(x == 1 || x == -1);
    }
}

// Section 149c: hand-written solutions in the Mini-C AST for the seven batch-1 problems the search did not solve. They show
// that each problem is solvable inside the grammar and the 150-node limit (sizes 23-41), so the 0/30 results are not an
// artefact of the references or of the size limit.
namespace {
using E = ExprKind;
ExprPtr V(int i){return makeVar(i);} ExprPtr C(int c){return makeIntConst(c);}
ExprPtr A(ExprPtr e){return makeArrayAccess(std::move(e));}
ExprPtr B(E k, ExprPtr a, ExprPtr b){return makeBinOp(k,std::move(a),std::move(b));}
ExprPtr Q(E k, ExprPtr a, ExprPtr b){return makeCompare(k,std::move(a),std::move(b));}
using L = std::vector<StmtPtr>;
template<class... T> L list(T... s){ L l; (l.push_back(std::move(s)),...); return l; }
StmtPtr S(int v, ExprPtr e){return makeAssign(v,std::move(e));}
StmtPtr inc(int v){return S(v,B(E::Add,V(v),C(1)));}
StmtPtr seq(L l){auto b=makeBlock(); b->body=std::move(l); return b;}

}  // namespace

TEST(fresh_unsolved_batch_1_problems_have_known_solutions_within_the_grammar) {
    Interpreter interp(20000);
    using K = FreshProblem::Kind;
  struct P{const char*name; FreshProblem::Kind k; StmtPtr prog;};
  std::vector<P> ps;
  ps.push_back({"sum_multiples_3_or_5",K::SumMultiples3Or5, seq(list(S(1,C(1)),S(2,C(0)),
     makeWhile(Q(E::Lt,V(1),A(C(0))), list(
        makeIf(Q(E::Eq,B(E::Mod,V(1),C(3)),C(0)), list(S(2,B(E::Add,V(2),V(1)))), list(
            makeIf(Q(E::Eq,B(E::Mod,V(1),C(5)),C(0)), list(S(2,B(E::Add,V(2),V(1))))))),
        inc(1))),
     makeAssignArray(C(0),V(2))))});
  ps.push_back({"binomial",K::Binomial, seq(list(S(1,A(C(0))),S(2,A(C(1))),S(3,C(1)),S(4,C(1)),
     makeWhile(Q(E::Le,V(4),V(2)), list(S(3,B(E::Div,B(E::Mul,V(3),B(E::Add,B(E::Sub,V(1),V(2)),V(4))),V(4))), inc(4))),
     makeAssignArray(C(0),V(3))))});
  ps.push_back({"digit_sum",K::DigitSum, seq(list(S(1,A(C(0))),S(2,C(0)),
     makeWhile(Q(E::Gt,V(1),C(0)), list(S(2,B(E::Add,V(2),B(E::Mod,V(1),C(10)))), S(1,B(E::Div,V(1),C(10))))),
     makeAssignArray(C(0),V(2))))});
  ps.push_back({"popcount",K::Popcount, seq(list(S(1,A(C(0))),S(2,C(0)),
     makeWhile(Q(E::Gt,V(1),C(0)), list(S(2,B(E::Add,V(2),B(E::Mod,V(1),C(2)))), S(1,B(E::Div,V(1),C(2))))),
     makeAssignArray(C(0),V(2))))});
  ps.push_back({"alternating_sum",K::AlternatingSum, seq(list(S(1,C(0)),S(2,C(0)),
     makeWhile(Q(E::Lt,V(1),V(0)), list(
        makeIf(Q(E::Eq,B(E::Mod,V(1),C(2)),C(0)), list(S(2,B(E::Add,V(2),A(V(1))))), list(S(2,B(E::Sub,V(2),A(V(1)))))),
        inc(1))),
     makeAssignArray(C(0),V(2))))});
  ps.push_back({"balanced_parentheses",K::BalancedParentheses, seq(list(S(1,C(0)),S(2,C(0)),S(3,C(1)),
     makeWhile(Q(E::Lt,V(1),V(0)), list(S(2,B(E::Add,V(2),A(V(1)))), makeIf(Q(E::Lt,V(2),C(0)), list(S(3,C(0)))), inc(1))),
     makeIf(Q(E::Ne,V(2),C(0)), list(S(3,C(0)))),
     makeAssignArray(C(0),V(3))))});
  ps.push_back({"count_pairs_with_sum",K::CountPairsWithSum, seq(list(S(1,C(1)),S(3,C(0)),
     makeWhile(Q(E::Lt,V(1),V(0)), list(S(2,B(E::Add,V(1),C(1))),
        makeWhile(Q(E::Lt,V(2),V(0)), list(
           makeIf(Q(E::Eq,B(E::Add,A(V(1)),A(V(2))),A(C(0))), list(inc(3))), inc(2))),
        inc(1))),
     makeAssignArray(C(0),V(3))))});
    for (auto& p : ps) {
        FreshProblem prob(p.k, 7, 1);
        CHECK(p.prog->size() <= 150);
        for (const Instance& inst : prob.generateInstances(1)) CHECK(prob.evaluate(*p.prog, inst, interp).status == ExecStatus::SUCCESS);
        for (const Instance& inst : prob.generateAdversarialInstances(1)) CHECK(prob.evaluate(*p.prog, inst, interp).status == ExecStatus::SUCCESS);
    }
}

TEST(section_150_pieces_are_byte_identical_when_off_and_change_programs_when_on) {
    GenConfig off;
    off.numVars = 8;
    off.combinatorBias = 1.0;
    off.digitLoopBias = off.counterModBias = off.signedParityBias = 0.0;
    GenConfig on = off;
    on.digitLoopBias = 1.0;
    auto text = [&](const GenConfig& cfg) {
        Random rng(7);
        std::string all;
        for (int i = 0; i < 40; ++i) all += generateRandomProgram(rng, 30, cfg)->toString();
        return all;
    };
    CHECK(text(off) == text(off));
    CHECK(text(on) != text(off));
    GenConfig counter = off;
    counter.counterModBias = 1.0;
    GenConfig signedSum = off;
    signedSum.signedParityBias = 1.0;
    CHECK(text(counter) != text(off));
    CHECK(text(signedSum) != text(off));
}

TEST(fresh_batch_2_references_match_hand_computed_values) {
    using K = FreshProblem::Kind;
    auto tr = FreshProblem::reference(K::MatrixTranspose, {1, 2, 3, 4});
    CHECK_EQ(tr[1], 3);
    CHECK_EQ(tr[2], 2);
    auto tr3 = FreshProblem::reference(K::MatrixTranspose, {1, 2, 3, 4, 5, 6, 7, 8, 9});
    CHECK_EQ(tr3[1], 4);
    CHECK_EQ(tr3[3], 2);
    CHECK_EQ(tr3[5], 8);
    CHECK_EQ(FreshProblem::reference(K::BinaryToDecimal, {1, 0, 1, 1})[0], 11);
    CHECK_EQ(FreshProblem::reference(K::RangeMaxMin, {3, -4, 9})[0], 13);
    auto ad = FreshProblem::reference(K::AdjacentDifferences, {5, 7, 4});
    CHECK_EQ(ad[0], 5);
    CHECK_EQ(ad[1], 2);
    CHECK_EQ(ad[2], -3);
    CHECK_EQ(FreshProblem::reference(K::Lcm, {4, 6})[0], 12);
    CHECK_EQ(FreshProblem::reference(K::Lcm, {7, 1})[0], 7);
    CHECK_EQ(FreshProblem::reference(K::MaxNonAdjacentSum, {5, 1, 1, 5})[0], 10);
    CHECK_EQ(FreshProblem::reference(K::MaxNonAdjacentSum, {2})[0], 2);
    CHECK_EQ(FreshProblem::reference(K::Isqrt, {0})[0], 0);
    CHECK_EQ(FreshProblem::reference(K::Isqrt, {24})[0], 4);
    CHECK_EQ(FreshProblem::reference(K::Isqrt, {25})[0], 5);
    CHECK_EQ(FreshProblem::reference(K::CountDivisors, {12})[0], 6);
    CHECK_EQ(FreshProblem::reference(K::CountDivisors, {1})[0], 1);
    CHECK_EQ(FreshProblem::reference(K::KthSmallest, {2, 9, 3, 3, 7})[0], 3);
    CHECK_EQ(FreshProblem::reference(K::KthSmallest, {3, 9, 3, 3, 7})[0], 7);
    CHECK_EQ(FreshProblem::reference(K::Josephus, {7, 3})[0], 4);
    CHECK_EQ(FreshProblem::reference(K::Josephus, {1, 5})[0], 1);
}

TEST(fresh_batch_2_builds_train_and_test_sets_that_its_references_solve) {
    using K = FreshProblem::Kind;
    for (K k : {K::MatrixTranspose, K::BinaryToDecimal, K::RangeMaxMin, K::AdjacentDifferences, K::Lcm, K::MaxNonAdjacentSum,
                K::Isqrt, K::CountDivisors, K::KthSmallest, K::Josephus}) {
        FreshProblem p(k, 7, 1);
        CHECK_EQ(p.generateInstances(1).size(), static_cast<size_t>(200));
        CHECK_EQ(p.generateAdversarialInstances(1).size(), static_cast<size_t>(2000));
        for (const Instance& inst : p.generateAdversarialInstances(1)) CHECK(p.isSolution(inst.input, inst.expectedOutput));
    }
}

TEST(fresh_unsolved_batch_2_problems_have_known_solutions_within_the_grammar) {
    Interpreter interp(20000);
    using K = FreshProblem::Kind;
    struct P { const char* name; K k; StmtPtr prog; };
    std::vector<P> ps;
    // out[i] = arr[i] - arr[i-1], filled from the back so that arr[i-1] is still the input value
    ps.push_back({"adjacent_differences", K::AdjacentDifferences, seq(list(S(1, B(E::Sub, V(0), C(1))),
        makeWhile(Q(E::Gt, V(1), C(0)), list(
            makeAssignArray(V(1), B(E::Sub, A(V(1)), A(B(E::Sub, V(1), C(1))))),
            S(1, B(E::Sub, V(1), C(1)))))))});
    // gcd by remainders, then arr[0] / g * arr[1]
    ps.push_back({"lcm", K::Lcm, seq(list(S(1, A(C(0))), S(2, A(C(1))),
        makeWhile(Q(E::Ne, V(2), C(0)), list(S(3, B(E::Mod, V(1), V(2))), S(1, V(2)), S(2, V(3)))),
        makeAssignArray(C(0), B(E::Mul, B(E::Div, A(C(0)), V(1)), A(C(1))))))});
    // survivor = (survivor + k) % i for i = 2..n; answer survivor + 1
    ps.push_back({"josephus", K::Josephus, seq(list(S(1, A(C(0))), S(2, A(C(1))), S(3, C(0)), S(4, C(2)),
        makeWhile(Q(E::Le, V(4), V(1)), list(S(3, B(E::Mod, B(E::Add, V(3), V(2)), V(4))), inc(4))),
        makeAssignArray(C(0), B(E::Add, V(3), C(1)))))});
    // n = floor(sqrt(len)); swap arr[i*n+j] and arr[j*n+i] for j > i
    ps.push_back({"matrix_transpose", K::MatrixTranspose, seq(list(S(1, C(0)),
        makeWhile(Q(E::Le, B(E::Mul, B(E::Add, V(1), C(1)), B(E::Add, V(1), C(1))), V(0)), list(inc(1))),
        S(2, C(0)),
        makeWhile(Q(E::Lt, V(2), V(1)), list(
            S(3, B(E::Add, V(2), C(1))),
            makeWhile(Q(E::Lt, V(3), V(1)), list(
                S(4, A(B(E::Add, B(E::Mul, V(2), V(1)), V(3)))),
                makeAssignArray(B(E::Add, B(E::Mul, V(2), V(1)), V(3)), A(B(E::Add, B(E::Mul, V(3), V(1)), V(2)))),
                makeAssignArray(B(E::Add, B(E::Mul, V(3), V(1)), V(2)), V(4)),
                inc(3))),
            inc(2)))))});
    for (auto& p : ps) {
        FreshProblem prob(p.k, 7, 1);
        CHECK(p.prog->size() <= 150);
        for (const Instance& inst : prob.generateInstances(1)) CHECK(prob.evaluate(*p.prog, inst, interp).status == ExecStatus::SUCCESS);
        for (const Instance& inst : prob.generateAdversarialInstances(1)) CHECK(prob.evaluate(*p.prog, inst, interp).status == ExecStatus::SUCCESS);
    }
}
