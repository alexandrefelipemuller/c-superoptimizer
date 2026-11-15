#include <climits>
#include <functional>
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
    CHECK(rejects("int f(int x) { return g(x); }"));
    CHECK(rejects("int f(int *a, int n) { return *a; }"));
    CHECK(rejects("int f(int x) { int y = x++; return y; }"));
    CHECK(rejects("int f(int x) { long y = x; return y; }"));
    CHECK(rejects("#define SQ(x) ((x) * (x))\nint f(int x) { return SQ(x); }"));  // function-like macro
    CHECK(rejects("#if 1\nint f(int x) { return x; }\n#endif"));
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
    CImport scratch = importCFunction("void f(int *a, int n) { a[1] = 1; }");
    scratch.abi.returnsValue = true;
    std::string c2 = emitCFunction(*scratch.program, scratch.abi);
    CHECK(c2.find("int arr[v0 > 0 ? v0 : 1];") != std::string::npos);
    // A result write that ends the function is a return, no copy needed.
    CImport tail = importCFunction("void f(int *a, int n) { a[0] = 1; }");
    tail.abi.returnsValue = true;
    std::string c3 = emitCFunction(*tail.program, tail.abi);
    CHECK(c3.find("return 1;") != std::string::npos);
    CHECK(c3.find("int arr[") == std::string::npos);
}

TEST(c_import_constant_defines_and_limits) {
    CImport imp = importCFunction(
        "#include <limits.h>\n#define LOW 10\n#define SPAN (LOW * 2)\n"
        "int f(int x) { if (x == INT_MIN) return INT_MAX; return x < LOW ? LOW : x > SPAN ? SPAN : x; }");
    for (int x : scalarValues) {
        bool ok = false;
        int r = runImport(imp, {x}, ok)[0];
        CHECK(ok);
        CHECK_EQ(r, x == INT_MIN ? INT_MAX : x < 10 ? 10 : x > 20 ? 20 : x);
    }
}

TEST(c_import_character_literals_are_int_constants) {
    CImport imp = importCFunction(
        "int f(int c) { if (c == '\\n') return '\\t' + '\\\\' + '\\'' + '\\0'; "
        "if (c == '\\x41') return '\\101' - 'A'; return c >= '0' && c <= '9' ? c - '0' : '?'; }");
    for (int c : std::vector<int>{'\n', 'A', '0', '7', '9', 'a', -1, 200}) {
        bool ok = false;
        int r = runImport(imp, {c}, ok)[0];
        CHECK(ok);
        int want = c == '\n' ? '\t' + '\\' + '\'' + '\0' : c == 'A' ? 0 : (c >= '0' && c <= '9') ? c - '0' : '?';
        CHECK_EQ(r, want);
    }
    CHECK(rejects("int f(int c) { return c == '\\xff'; }"));  // signedness of char
    CHECK(rejects("int f(int c) { return c == 'ab'; }"));
    CHECK(rejects("int f(int c) { return c == ''; }"));
    CHECK(rejects("int f(int c) { return c == '\\q'; }"));
}

TEST(c_import_unsigned_compares_shifts_literals_and_helpers) {
    using U = unsigned;
    static const auto I = [](U v) { return static_cast<int>(v); };
    struct Case {
        const char* src;
        std::function<int(int, int)> native;  // on the int bits of the arguments
    };
    std::vector<Case> cases = {
        {"int f(int c, int d) { return (unsigned)c - 'a' < 26; }", [](int c, int) { return U(c) - 'a' < 26u; }},
        {"unsigned f(unsigned x, int k) { return (x >> 3) + (x >> 31) + (x << 4) + (x >> (k & 31)); }",
         [&](int x, int k) { return I((U(x) >> 3) + (U(x) >> 31) + (U(x) << 4) + (U(x) >> (k & 31))); }},
        {"int f(int a, unsigned b) { return a < b ? 1 : a > b ? 2 : a <= b ? 3 : 4; }",
         [](int a, int b) { return U(a) < U(b) ? 1 : U(a) > U(b) ? 2 : 3; }},
        {"unsigned f(unsigned x, int y) { unsigned n = 0; while (x) { n += x & 1; x >>= 1; } return n; }",
         [](int x, int) { return __builtin_popcount(U(x)); }},
        {"int f(int x, int y) { return x > 0xFFFFFFF0 ? 1 : (x + 10u < 20u) + (y >= 0x80000000); }",
         [](int x, int y) { return U(x) > 0xFFFFFFF0u ? 1 : int(U(x) + 10u < 20u) + int(U(y) >= 0x80000000u); }},
        {"int f(int a, unsigned b) { return (a ? b : 0) > 5; }", [](int a, int b) { return (a ? U(b) : 0u) > 5u; }},
        {"int f(unsigned x, unsigned y) { return (int)x < 0 ? (int)y >> 1 : (int)x < (int)y; }",
         [](int x, int y) { return x < 0 ? y >> 1 : x < y; }},
        {"static unsigned g(unsigned v) { return v >> 1; }\nint f(int x, int y) { return g(x) > 3 && y < 2; }",
         [](int x, int y) { return (U(x) >> 1) > 3u && y < 2; }},
        {"uint32_t f(uint32_t x, unsigned int y) { for (int i = 0; i < 2; i++) { if (x >= y) break; x = x * 3u; } "
         "return x; }",
         [&](int x, int y) {
             U v = U(x);
             for (int i = 0; i < 2 && v < U(y); i++) v *= 3u;
             return I(v);
         }},
    };
    std::vector<int> values = scalarValues;
    for (int v : {97, 122, 123, 96, 5, 6, 17, 31, 32, -16, -15, -20, -2}) values.push_back(v);
    for (const auto& c : cases) {
        CImport imp = importCFunction(c.src);
        for (int x : values)
            for (int y : values) {
                bool ok = false;
                auto r = runImport(imp, {x, y}, ok);
                CHECK(ok);
                if (ok) CHECK_EQ(r[0], c.native(x, y));
            }
    }
    // A variable shift count outside 0..31 is undefined in C: a run-time error, as for int.
    CImport shift = importCFunction("unsigned f(unsigned x, int k) { return x >> k; }");
    bool ok = true;
    runImport(shift, {-1, 32}, ok);
    CHECK(!ok);
    CHECK(rejects("unsigned f(unsigned x) { return x / 3; }"));  // unsigned division: not yet
    CHECK(rejects("int f(int x) { unsigned y = x; return y % 3; }"));
    CHECK(rejects("int f(int x) { unsigned char y = x; return y; }"));
    CHECK(rejects("int f(int x) { return (unsigned long)x; }"));
    CHECK(rejects("int f(unsigned *a, int n) { return a[0]; }"));
    CHECK(rejects("int f(int x) { return x + 1UL; }"));
    CHECK(rejects("int f(int x) { return x < 3000000000; }"));  // a long in C
}

TEST(c_import_inlines_single_return_helpers_once_per_argument) {
    // pos() reads its argument twice; a[i + 1] is guarded by the && and
    // must not be read at i = n - 1.
    CImport imp = importCFunction(
        "static inline int pos(int v) { return v > 0 && v < 100; }\n"
        "static int twice(int v) { return pos(v) + pos(v); }\n"
        "int f(int *a, int n) { int c = 0; for (int i = 0; i < n; i++) if (i + 1 < n && pos(a[i + 1])) c += twice(a[i]);"
        " return c; }");
    CHECK_EQ(imp.abi.name, std::string("f"));  // the function no other one calls
    bool ok = false;
    CHECK_EQ(runImport(imp, {5, 7, -1, 3}, ok)[0], 2);  // i = 0 (7 > 0, a[0] = 5 counts twice)
    CHECK(ok);
    CHECK_EQ(runImport(imp, {5, 7, 8, 200}, ok)[0], 4);
    CHECK(ok);
    CHECK(rejects("static int g(int x) { int y = x; return y; }\nint f(int x) { return g(x); }"));
    CHECK(rejects("static int g(int x) { return g(x - 1); }\nint f(int x) { return g(x); }"));
    CHECK(rejects("int f(int x) { return abs(x); }"));  // not defined in the file
    // A helper that cannot be inlined is fine as long as it is not called.
    bool threw = false;
    try {
        importCFunction("static int g(int *a) { return a[0]; }\nint f(int x) { return x; }", "f");
    } catch (const CImportError&) {
        threw = true;
    }
    CHECK(!threw);
}

TEST(c_import_continue_in_for_with_step_and_do_while) {
    // continue must still run i++; break leaves the outer loop.
    CImport imp = importCFunction(
        "int f(int *a, int n) { int s = 0; for (int i = 0; i < n; i++) { if (a[i] < 0) continue;"
        " if (a[i] == 99) break; for (int j = 0; j < 2; j++) { if (j == 0) continue; s += a[i]; } } return s; }");
    std::vector<std::vector<int>> cases = {{1, -2, 3}, {4, 99, 5}, {-1, -1}, {7}};
    for (const auto& in : cases) {
        int expect = 0;
        for (int v : in) {
            if (v < 0) continue;
            if (v == 99) break;
            expect += v;
        }
        bool ok = false;
        CHECK_EQ(runImport(imp, in, ok)[0], expect);
        CHECK(ok);
    }
    CImport dw = importCFunction(
        "int g(int x) { int c = 0; do { x = x >> 1; if (x & 1) continue; c++; } while (x != 0 && x != -1); return c; }");
    for (int x : scalarValues) {
        int y = x, expect = 0;
        do {
            y = y >> 1;
            if (y & 1) continue;
            expect++;
        } while (y != 0 && y != -1);
        bool ok = false;
        CHECK_EQ(runImport(dw, {x}, ok)[0], expect);
        CHECK(ok);
    }
}
