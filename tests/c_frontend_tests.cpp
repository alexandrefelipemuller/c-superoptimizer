#include <climits>
#include <string>
#include <vector>

#include "ast/Expr.hpp"
#include "ast/Stmt.hpp"
#include "interpreter/Interpreter.hpp"
#include "superopt/CFrontend.hpp"
#include "test_framework.hpp"

using namespace algosearch;

namespace {

// Runs an imported function on one argument list in wraparound mode; sets
// `ok` false on a runtime error or timeout.
std::vector<int> runImport(const CImport& imp, const std::vector<int>& args, bool& ok) {
    Interpreter interp(1'000'000);
    interp.setWrapArithmetic(true);
    std::vector<int> out;
    ok = interp.run(*imp.program, args, imp.abi.numVars, out) == RunStatus::COMPLETED;
    return ok ? abiResult(imp.abi, out) : std::vector<int>{};
}

bool rejects(const std::string& src) {
    try {
        importCFunction(src);
    } catch (const CImportError&) {
        return true;
    }
    return false;
}

const std::vector<int> scalarValues = {0, 1, -1, 2, 7, 12, 64, -64, 1000, -12345, INT_MAX, INT_MIN, INT_MIN + 1};

}  // namespace

TEST(interpreter_add_and_sub_wrap_in_both_modes) {
    for (bool wrap : {false, true}) {
        auto block = makeBlock();
        block->body.push_back(makeAssignArray(makeIntConst(0), makeBinOp(ExprKind::Add, makeArrayAccess(makeIntConst(0)),
                                                                         makeIntConst(1))));
        block->body.push_back(makeAssignArray(makeIntConst(1), makeBinOp(ExprKind::Sub, makeArrayAccess(makeIntConst(1)),
                                                                         makeIntConst(1))));
        Interpreter interp(1000);
        interp.setWrapArithmetic(wrap);
        std::vector<int> out;
        CHECK(interp.run(*block, {INT_MAX, INT_MIN}, 1, out) == RunStatus::COMPLETED);
        CHECK_EQ(out[0], INT_MIN);
        CHECK_EQ(out[1], INT_MAX);
    }
}

TEST(interpreter_mul_overflow_is_error_by_default_and_wraps_when_enabled) {
    auto block = makeBlock();
    block->body.push_back(makeAssignArray(makeIntConst(0), makeBinOp(ExprKind::Mul, makeArrayAccess(makeIntConst(0)),
                                                                     makeIntConst(31))));
    std::vector<int> out;
    Interpreter strict(1000);
    CHECK(strict.run(*block, {INT_MAX}, 1, out) == RunStatus::RUNTIME_ERROR);
    Interpreter wrap(1000);
    wrap.setWrapArithmetic(true);
    CHECK(wrap.run(*block, {INT_MAX}, 1, out) == RunStatus::COMPLETED);
    CHECK_EQ(out[0], static_cast<int>(static_cast<unsigned>(INT_MAX) * 31u));
    // INT_MIN / -1 still traps, as it does in C even with -fwrapv.
    auto div = makeBlock();
    div->body.push_back(makeAssignArray(makeIntConst(0), makeBinOp(ExprKind::Div, makeArrayAccess(makeIntConst(0)),
                                                                   makeIntConst(-1))));
    CHECK(wrap.run(*div, {INT_MIN}, 1, out) == RunStatus::RUNTIME_ERROR);
}

TEST(c_import_scalar_function_matches_native_popcount) {
    CImport imp = importCFunction(
        "int popcount(int x) { int c = 0; while (x != 0) { x &= x - 1; c++; } return c; }");
    CHECK(imp.abi.kind == CFunctionAbi::Kind::Scalars);
    CHECK_EQ(static_cast<int>(imp.abi.params.size()), 1);
    for (int x : scalarValues) {
        bool ok = false;
        std::vector<int> r = runImport(imp, {x}, ok);
        CHECK(ok);
        CHECK_EQ(r[0], __builtin_popcount(static_cast<unsigned>(x)));
    }
}

TEST(c_import_operators_precedence_ternary_and_unary) {
    CImport imp = importCFunction(
        "int f(int a, int b) { int t = a * 3 + b % 5 - (a ^ b) << 1; return t > 0 ? ~t : !b + -a; }");
    auto native = [](int a, int b) {
        unsigned t0 = static_cast<unsigned>(a) * 3u + static_cast<unsigned>(b % 5) - static_cast<unsigned>(a ^ b);
        int t = static_cast<int>(t0 << 1);
        return t > 0 ? ~t : static_cast<int>(!b + (0u - static_cast<unsigned>(a)));
    };
    for (int a : {0, 1, -7, 40, 999, -999})
        for (int b : {0, 3, -3, 17, 1 << 20}) {
            bool ok = false;
            std::vector<int> r = runImport(imp, {a, b}, ok);
            CHECK(ok);
            CHECK_EQ(r[0], native(a, b));
        }
}

TEST(c_import_preserves_short_circuit_guarding_array_reads) {
    CImport imp = importCFunction("int f(int *a, int n) { int i = 0; while (i < n && a[i] > 0) i++; return i; }");
    CHECK(imp.abi.kind == CFunctionAbi::Kind::Array);
    bool ok = false;
    CHECK_EQ(runImport(imp, {3, 1, 2}, ok)[0], 3);  // i reaches n: a[n] must not be read
    CHECK(ok);
    CHECK_EQ(runImport(imp, {3, -1, 2}, ok)[0], 1);
    CHECK(ok);
}

TEST(c_import_for_loop_and_void_array_function) {
    CImport imp = importCFunction(
        "void isort(int *a, int n) { for (int i = 1; i < n; i++) { int k = a[i]; int j = i - 1;"
        " while (j >= 0 && a[j] > k) { a[j + 1] = a[j]; j--; } a[j + 1] = k; } }");
    CHECK(!imp.abi.returnsValue);
    bool ok = false;
    std::vector<int> r = runImport(imp, {5, -1, 3, 3, 0, 9}, ok);
    CHECK(ok);
    CHECK(r == std::vector<int>({-1, 0, 3, 3, 5, 9}));
}

TEST(c_import_length_parameter_written_gets_private_copy) {
    CImport imp = importCFunction("int f(int *a, int n) { int s = 0; while (n > 0) { n--; s += a[n]; } return s; }");
    // v0 itself must stay the array length register: nothing assigns it.
    bool assignsV0 = false;
    for (const auto& s : imp.program->body)
        if (s->kind == StmtKind::Assign && s->varIndex == 0) assignsV0 = true;
    CHECK(!assignsV0);
    bool ok = false;
    CHECK_EQ(runImport(imp, {4, 5, 6}, ok)[0], 15);
    CHECK(ok);
}

TEST(c_import_do_while_and_break) {
    CImport imp = importCFunction(
        "int ctz(int x) { if (x == 0) return 32; int n = 0; do { if (x & 1) break; x = x >> 1; n += 1; } while (1);"
        " return n; }");
    for (int x : scalarValues) {
        bool ok = false;
        std::vector<int> r = runImport(imp, {x}, ok);
        CHECK(ok);
        CHECK_EQ(r[0], x == 0 ? 32 : __builtin_ctz(static_cast<unsigned>(x)));
    }
}

TEST(c_import_rejects_constructs_outside_the_subset) {
    CHECK(rejects("unsigned f(unsigned x) { return x; }"));
    CHECK(rejects("int f(int x) { return g(x); }"));
    CHECK(rejects("int f(int *a, int n) { return *a; }"));
    CHECK(rejects("int f(int x) { int y = x++; return y; }"));
    CHECK(rejects("int f(int x) { for (int i = 0; i < x; i++) { if (i == 2) continue; } return x; }"));
    CHECK(rejects("int f(int x) { long y = x; return y; }"));
    CHECK(rejects("#define K 3\nint f(int x) { return x + K; }"));
    CHECK(rejects("int f(int x) { return x; } int g(int x) { return x; }"));  // ambiguous without a name
    CHECK(!rejects("#include <stdint.h>\nint32_t f(int32_t x) { return x; }"));
}

TEST(c_import_picks_named_function) {
    CImport imp = importCFunction("int f(int x) { return x; } int g(int x) { return x + 1; }", "g");
    CHECK_EQ(imp.abi.name, std::string("g"));
    bool ok = false;
    CHECK_EQ(runImport(imp, {41}, ok)[0], 42);
}

TEST(c_emit_int_array_function_leaves_caller_array_untouched) {
    CImport imp = importCFunction("int f(int *a, int n) { int s = 0; for (int i = 0; i < n; i++) s += a[i]; return s; }");
    std::string c = emitCFunction(*imp.program, imp.abi);
    CHECK(c.find("a[0] =") == std::string::npos);
    CHECK(c.find("return v") != std::string::npos);
    // A program that uses the array as scratch space works on a copy.
    CImport scratch = importCFunction("void f(int *a, int n) { a[0] = 1; }");
    scratch.abi.returnsValue = true;
    std::string c2 = emitCFunction(*scratch.program, scratch.abi);
    CHECK(c2.find("int arr[v0 > 0 ? v0 : 1];") != std::string::npos);
}
