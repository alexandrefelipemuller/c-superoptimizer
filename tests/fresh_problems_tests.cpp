#include "core/Random.hpp"
#include "problems/FreshProblem.hpp"
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
