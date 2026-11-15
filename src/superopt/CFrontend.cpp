#include "superopt/CFrontend.hpp"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <sstream>

#include "ast/Expr.hpp"

namespace algosearch {

namespace {

// ---------------------------------------------------------------- lexer

enum class Tok { Ident, Number, Punct, End };

struct Token {
    Tok kind;
    std::string text;
    int line;
    long long value = 0;
    bool isUnsigned = false;  // a Number of type unsigned int
};

[[noreturn]] void failAt(int line, const std::string& what) {
    throw CImportError("line " + std::to_string(line) + ": " + what);
}

using Defines = std::map<std::string, std::vector<Token>>;

std::vector<Token> lexWith(const std::string& src, Defines& defines);

// Object-like macros: `#define NAME body`, the body lexed with the macros
// defined so far. Function-like macros are rejected.
void defineMacro(const std::string& directive, int line, Defines& defines) {
    std::smatch m;
    if (!std::regex_search(directive, m, std::regex("^#\\s*define\\s+([A-Za-z_][A-Za-z0-9_]*)(.*)$")))
        failAt(line, "malformed #define: " + directive);
    std::string rest = m[2].str();
    if (!rest.empty() && rest[0] == '(') failAt(line, "function-like macro not supported: " + directive);
    std::vector<Token> body = lexWith(rest, defines);
    body.pop_back();  // End
    for (auto& t : body) t.line = line;
    defines[m[1].str()] = body;
}

std::vector<Token> lex(const std::string& src) {
    Defines defines;
    defines["INT_MAX"] = lexWith("2147483647", defines);
    defines["INT_MIN"] = lexWith("(-2147483647 - 1)", defines);
    for (auto& [name, body] : defines) body.pop_back();
    return lexWith(src, defines);
}

std::vector<Token> lexWith(const std::string& src, Defines& defines) {
    static const char* puncts[] = {"<<=", ">>=", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||", "++", "--", "+=",
                                   "-=",  "*=",  "/=", "%=", "&=", "|=", "^=", "->", "+",  "-",  "*",  "/",  "%",
                                   "&",   "|",   "^",  "~",  "!",  "<",  ">",  "=",  "?",  ":",  ";",  ",",  "(",
                                   ")",   "{",   "}",  "[",  "]",  "."};
    std::vector<Token> out;
    size_t i = 0;
    int line = 1;
    bool lineStart = true;
    while (i < src.size()) {
        char c = src[i];
        if (c == '\n') {
            ++line;
            ++i;
            lineStart = true;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        if (lineStart && c == '#') {
            size_t e = src.find('\n', i);
            std::string directive = src.substr(i, e == std::string::npos ? std::string::npos : e - i);
            if (std::regex_search(directive, std::regex("^#\\s*define\\b")))
                defineMacro(directive, line, defines);
            else if (!std::regex_search(directive, std::regex("^#\\s*include\\b")))
                failAt(line, "preprocessor directive not supported (only #include and constant #define): " +
                                 directive);
            i = e == std::string::npos ? src.size() : e;
            continue;
        }
        lineStart = false;
        if (src.compare(i, 2, "//") == 0) {
            while (i < src.size() && src[i] != '\n') ++i;
            continue;
        }
        if (src.compare(i, 2, "/*") == 0) {
            size_t e = src.find("*/", i + 2);
            if (e == std::string::npos) failAt(line, "unterminated comment");
            for (size_t k = i; k < e; ++k)
                if (src[k] == '\n') ++line;
            i = e + 2;
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t s = i;
            while (i < src.size() && (std::isalnum(static_cast<unsigned char>(src[i])) || src[i] == '_')) ++i;
            auto macro = defines.find(src.substr(s, i - s));
            if (macro != defines.end()) {
                for (Token t : macro->second) {
                    t.line = line;
                    out.push_back(t);
                }
                continue;
            }
            out.push_back({Tok::Ident, src.substr(s, i - s), line});
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            size_t s = i;
            while (i < src.size() && std::isalnum(static_cast<unsigned char>(src[i]))) ++i;
            std::string text = src.substr(s, i - s);
            size_t used = 0;
            unsigned long long v = 0;
            try {
                v = std::stoull(text, &used, 0);
            } catch (...) {
                failAt(line, "bad integer literal " + text);
            }
            std::string suffix = text.substr(used);
            if (!suffix.empty() && suffix != "u" && suffix != "U")
                failAt(line, "integer literal suffix not supported: " + text);
            // C's literal types, for 32-bit int: a `u` suffix makes it
            // unsigned; an unsuffixed hex or octal literal that does not fit
            // in int is unsigned; a decimal one would be long (rejected).
            bool radix = text.size() > 1 && text[0] == '0';
            bool isUnsigned = !suffix.empty() || (radix && v > static_cast<unsigned long long>(INT_MAX));
            if (v > 0xFFFFFFFFull) failAt(line, "integer literal does not fit in 32 bits: " + text);
            // INT_MAX + 1 (decimal) is accepted only so that `-2147483648` can
            // be written; the parser rejects it anywhere else.
            if (!isUnsigned && v > static_cast<unsigned long long>(INT_MAX) + 1)
                failAt(line, "integer literal does not fit in int: " + text);
            Token t{Tok::Number, text, line};
            t.value = static_cast<long long>(v);
            t.isUnsigned = isUnsigned;
            out.push_back(t);
            continue;
        }
        if (c == '\'') {
            // A character literal is an int constant. Only one ASCII character
            // (or escape): above 127 its value depends on char's signedness.
            int at = line;
            size_t s = i++;
            if (i >= src.size() || src[i] == '\'' || src[i] == '\n') failAt(at, "empty character literal");
            unsigned long v = 0;
            if (src[i] != '\\') {
                v = static_cast<unsigned char>(src[i++]);
            } else {
                ++i;
                char e = i < src.size() ? src[i] : '\n';
                static const std::string simple = "ntr0abfv\\'\"?";
                static const unsigned char values[] = {'\n', '\t', '\r', 0, '\a', '\b', '\f', '\v', '\\', '\'', '"', '?'};
                if (e == 'x') {
                    size_t h = ++i;
                    while (i < src.size() && std::isxdigit(static_cast<unsigned char>(src[i]))) ++i;
                    if (i == h || i - h > 2) failAt(at, "bad \\x escape in character literal");
                    v = std::stoul(src.substr(h, i - h), nullptr, 16);
                } else if (e >= '0' && e <= '7') {
                    size_t o = i;
                    while (i < src.size() && i - o < 3 && src[i] >= '0' && src[i] <= '7') ++i;
                    v = std::stoul(src.substr(o, i - o), nullptr, 8);
                } else if (simple.find(e) != std::string::npos) {
                    v = values[simple.find(e)];
                    ++i;
                } else {
                    failAt(at, std::string("unknown escape in character literal: \\") + e);
                }
            }
            if (i >= src.size() || src[i] != '\'')
                failAt(at, "multi-character or unterminated character literal not supported");
            ++i;
            if (v > 127) failAt(at, "character literal above 127 not supported (depends on char signedness)");
            Token t{Tok::Number, src.substr(s, i - s), at};
            t.value = static_cast<long long>(v);
            out.push_back(t);
            continue;
        }
        bool matched = false;
        for (const char* p : puncts) {
            size_t n = std::char_traits<char>::length(p);
            if (src.compare(i, n, p) == 0) {
                out.push_back({Tok::Punct, p, line});
                i += n;
                matched = true;
                break;
            }
        }
        if (!matched) failAt(line, std::string("unexpected character '") + c + "'");
    }
    out.push_back({Tok::End, "", line});
    return out;
}

// ------------------------------------------------------ C expression tree

// A C integer type of the subset (log 197): 8, 16 or 32 bits, signed or
// unsigned (plain `char` is signed, as on gcc/x86). Values narrower than 32
// bits promote to int in every expression, so only storage (variables,
// parameters, results, casts) truncates.
struct CType {
    int bits = 32;
    bool isUnsigned = false;
    std::string spelling = "int";  // as the emitter writes it (no <stdint.h> names)
    bool narrow() const { return bits < 32; }
    bool promotedUnsigned() const { return bits == 32 && isUnsigned; }
    bool same(const CType& o) const { return bits == o.bits && isUnsigned == o.isUnsigned; }
};

struct CExpr {
    std::string op;  // "num", "id", "index", unary "neg" "~" "!", binary C operator, "?:"
    long long value = 0;
    bool isUnsigned = false;  // "num": the literal's type
    CType type;               // "cast": the target type
    std::string name;
    int line = 0;
    std::vector<std::unique_ptr<CExpr>> kids;
};
using CExprPtr = std::unique_ptr<CExpr>;

CExprPtr node(const std::string& op, int line) {
    auto e = std::make_unique<CExpr>();
    e->op = op;
    e->line = line;
    return e;
}

const std::map<std::string, ExprKind>& binaryKinds() {
    static const std::map<std::string, ExprKind> m = {
        {"+", ExprKind::Add},     {"-", ExprKind::Sub},    {"*", ExprKind::Mul},    {"/", ExprKind::Div},
        {"%", ExprKind::Mod},     {"&", ExprKind::BitAnd}, {"|", ExprKind::BitOr},  {"^", ExprKind::BitXor},
        {"<<", ExprKind::Shl},    {">>", ExprKind::Shr},   {"<", ExprKind::Lt},     {">", ExprKind::Gt},
        {"<=", ExprKind::Le},     {">=", ExprKind::Ge},    {"==", ExprKind::Eq},    {"!=", ExprKind::Ne}};
    return m;
}

// C binary precedence, higher binds tighter.
int precedence(const std::string& op) {
    if (op == "||") return 1;
    if (op == "&&") return 2;
    if (op == "|") return 3;
    if (op == "^") return 4;
    if (op == "&") return 5;
    if (op == "==" || op == "!=") return 6;
    if (op == "<" || op == ">" || op == "<=" || op == ">=") return 7;
    if (op == "<<" || op == ">>") return 8;
    if (op == "+" || op == "-") return 9;
    if (op == "*" || op == "/" || op == "%") return 10;
    return 0;
}

// Whether evaluating `e` can never be a runtime error, so evaluating it
// eagerly (no short circuit) is unobservable.
bool cannotFail(const CExpr& e) {
    if (e.op == "index" || e.op == "/" || e.op == "%" || e.op == "<<" || e.op == ">>") return false;
    for (const auto& k : e.kids)
        if (!cannotFail(*k)) return false;
    return true;
}

bool isBoolKind(ExprKind k) {
    return k == ExprKind::Lt || k == ExprKind::Gt || k == ExprKind::Le || k == ExprKind::Ge || k == ExprKind::Eq ||
           k == ExprKind::Ne;
}

// ------------------------------------------------------------- translator

struct Translator {
    std::vector<Token> toks;
    size_t pos = 0;
    CFunctionAbi abi;
    std::string arrayName;  // Array ABI: the pointer parameter
    std::vector<std::map<std::string, int>> scopes;
    std::map<int, CType> varTypes;  // Mini-C variables declared other than int
    CType returnType;
    int nextVar = 1;
    // Enclosing loops, innermost last. `continue` is direct in a while loop.
    // In a for loop with a step or a do-while it must still run the step (the
    // condition): the body is then re-translated inside `while (1) { B; break; }`,
    // where `continue` is that inner loop's break and `break` sets `flag`.
    struct LoopCtx {
        bool direct;
        bool wrapped = false;
        int flag = -1;
        bool needsWrap = false;
        bool usedBreak = false;
    };
    std::vector<LoopCtx> loops;
    // Helpers: other functions of the file whose body is `return expr;`,
    // inlined at each call with their arguments evaluated once, in order.
    struct Helper {
        std::vector<std::string> params;
        std::vector<CType> paramTypes;
        CType returnType;
        CExprPtr expr;
        std::string error;  // why it cannot be inlined
    };
    std::map<std::string, Helper> helpers;
    std::vector<std::string> inlining;

    const Token& peek(size_t ahead = 0) const { return toks[std::min(pos + ahead, toks.size() - 1)]; }
    bool isPunct(const char* p, size_t ahead = 0) const {
        return peek(ahead).kind == Tok::Punct && peek(ahead).text == p;
    }
    bool isWord(const char* w, size_t ahead = 0) const {
        return peek(ahead).kind == Tok::Ident && peek(ahead).text == w;
    }
    bool eat(const char* p) {
        if (!isPunct(p)) return false;
        ++pos;
        return true;
    }
    void expect(const char* p) {
        if (!eat(p)) failAt(peek().line, std::string("expected '") + p + "', found '" + peek().text + "'");
    }
    std::string ident() {
        if (peek().kind != Tok::Ident) failAt(peek().line, "expected an identifier, found '" + peek().text + "'");
        return toks[pos++].text;
    }

    // The integer types of up to 32 bits: `int`, `unsigned`, `char`, `short`
    // with `signed`/`unsigned`, the <stdint.h> names of 8, 16 and 32 bits,
    // each optionally `const`. `long` and the rest are rejected.
    bool atIntTypeAt(size_t k) const {
        if (isWord("const", k)) ++k;
        for (const char* w : {"int", "int32_t", "signed", "unsigned", "uint32_t", "char", "short", "uint8_t", "int8_t",
                              "uint16_t", "int16_t"})
            if (isWord(w, k)) return true;
        return false;
    }
    bool atIntType() const { return atIntTypeAt(0); }
    // Consumes one of those types.
    CType intType() {
        if (isWord("const")) ++pos;
        int line = peek().line;
        std::string t = ident();
        auto make = [](int bits, bool u, const char* spelling) {
            CType c;
            c.bits = bits;
            c.isUnsigned = u;
            c.spelling = spelling;
            return c;
        };
        CType c;
        if (t == "int" || t == "int32_t") {
            c = make(32, false, "int");
        } else if (t == "uint32_t") {
            c = make(32, true, "unsigned");
        } else if (t == "uint8_t") {
            c = make(8, true, "unsigned char");
        } else if (t == "int8_t") {
            c = make(8, false, "signed char");
        } else if (t == "uint16_t") {
            c = make(16, true, "unsigned short");
        } else if (t == "int16_t") {
            c = make(16, false, "short");
        } else if (t == "char") {
            c = make(8, false, "char");
        } else if (t == "short") {
            if (isWord("int")) ++pos;
            c = make(16, false, "short");
        } else if (t == "signed" || t == "unsigned") {
            bool u = t == "unsigned";
            if (isWord("long")) failAt(line, "type '" + t + " long' not supported (up to 32 bits)");
            if (isWord("char")) {
                ++pos;
                c = make(8, u, u ? "unsigned char" : "signed char");
            } else if (isWord("short")) {
                ++pos;
                if (isWord("int")) ++pos;
                c = make(16, u, u ? "unsigned short" : "short");
            } else {
                if (isWord("int")) ++pos;
                c = make(32, u, u ? "unsigned" : "int");
            }
        } else {
            failAt(line, "type '" + t + "' not supported (integers of up to 32 bits)");
        }
        if (isWord("const")) ++pos;
        return c;
    }
    CType varType(int v) const {
        auto it = varTypes.find(v);
        return it == varTypes.end() ? CType() : it->second;
    }
    // A value converted to `t`: the same bits for 32-bit types; truncated to
    // the low bits, zero- or sign-extended, for narrower ones.
    static ExprPtr convert(ExprPtr x, const CType& t) {
        if (!t.narrow()) return x;
        int shift = 32 - t.bits;
        if (x->kind == ExprKind::IntConst) {
            uint32_t low = static_cast<uint32_t>(x->intValue) & ((1u << t.bits) - 1);
            if (!t.isUnsigned && (low >> (t.bits - 1))) low |= ~((1u << t.bits) - 1);
            return makeIntConst(static_cast<int>(low));
        }
        if (t.isUnsigned) return makeBinOp(ExprKind::BitAnd, std::move(x), makeIntConst((1 << t.bits) - 1));
        return makeBinOp(ExprKind::Shr, makeBinOp(ExprKind::Shl, std::move(x), makeIntConst(shift)),
                         makeIntConst(shift));
    }
    void rejectOtherTypes() {
        static const char* other[] = {"long",   "float",  "double", "int64_t", "uint64_t", "size_t", "_Bool",
                                      "bool",   "struct", "union",  "enum",    "typedef",  "goto",   "switch"};
        if (peek().kind != Tok::Ident) return;
        for (const char* w : other)
            if (peek().text == w) failAt(peek().line, "'" + peek().text + "' not supported in this prototype");
    }

    int lookup(const std::string& name, int line) const {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto f = it->find(name);
            if (f != it->end()) return f->second;
        }
        if (name == arrayName) failAt(line, "array '" + name + "' used as a value (pointer arithmetic not supported)");
        failAt(line, "unknown identifier '" + name + "'");
    }

    // ------------------------------------------------------ expressions

    CExprPtr parseExpr() { return parseConditional(); }

    CExprPtr parseConditional() {
        CExprPtr c = parseBinary(1);
        if (!isPunct("?")) return c;
        int line = peek().line;
        ++pos;
        auto e = node("?:", line);
        e->kids.push_back(std::move(c));
        e->kids.push_back(parseExpr());
        expect(":");
        e->kids.push_back(parseConditional());
        return e;
    }

    CExprPtr parseBinary(int minPrec) {
        CExprPtr lhs = parseUnary();
        for (;;) {
            if (peek().kind != Tok::Punct) return lhs;
            std::string op = peek().text;
            int p = precedence(op);
            if (p == 0 || p < minPrec) return lhs;
            int line = peek().line;
            ++pos;
            CExprPtr rhs = parseBinary(p + 1);
            auto e = node(op, line);
            e->kids.push_back(std::move(lhs));
            e->kids.push_back(std::move(rhs));
            lhs = std::move(e);
        }
    }

    CExprPtr parseUnary() {
        int line = peek().line;
        if (eat("-")) {
            CExprPtr k = parseUnary();
            if (k->op == "num") {
                k->value = -k->value;
                return k;
            }
            auto e = node("neg", line);
            e->kids.push_back(std::move(k));
            return e;
        }
        if (eat("+")) return parseUnary();
        if (isPunct("~") || isPunct("!")) {
            auto e = node(toks[pos++].text, line);
            e->kids.push_back(parseUnary());
            return e;
        }
        if (isPunct("++") || isPunct("--") || isPunct("*") || isPunct("&"))
            failAt(line, "'" + peek().text + "' inside an expression not supported (side effects / pointers)");
        // A cast between int and unsigned keeps the bits and changes the type;
        // a cast to a narrower type truncates.
        {
            size_t k = 1;
            if (isWord("const", k)) ++k;
            if (isPunct("(") && isWord("long", k)) failAt(line, "cast to 'long' not supported");
        }
        if (isPunct("(") && atIntTypeAt(1)) {
            ++pos;
            auto e = node("cast", line);
            e->type = intType();
            expect(")");
            e->kids.push_back(parseUnary());
            return e;
        }
        return parsePostfix();
    }

    CExprPtr parsePostfix() {
        const Token& t = peek();
        CExprPtr e;
        if (t.kind == Tok::Number) {
            e = node("num", t.line);
            e->value = t.value;
            e->isUnsigned = t.isUnsigned;
            ++pos;
        } else if (t.kind == Tok::Ident) {
            e = node("id", t.line);
            e->name = t.text;
            ++pos;
            if (eat("(")) {
                auto call = node("call", t.line);
                call->name = t.text;
                if (!eat(")")) {
                    do call->kids.push_back(parseExpr());
                    while (eat(","));
                    expect(")");
                }
                e = std::move(call);
            } else if (isPunct("[")) {
                if (t.text != arrayName) failAt(t.line, "'" + t.text + "' is not the array parameter");
                ++pos;
                auto ix = node("index", t.line);
                ix->kids.push_back(parseExpr());
                expect("]");
                e = std::move(ix);
            }
        } else if (eat("(")) {
            e = parseExpr();
            if (isPunct(",")) failAt(peek().line, "comma operator not supported");
            expect(")");
        } else {
            failAt(t.line, "unexpected '" + t.text + "' in expression");
        }
        static const char* assigns[] = {"=",  "++", "--", "+=", "-=", "*=",  "/=",
                                        "%=", "&=", "|=", "^=", "<<=", ">>="};
        for (const char* a : assigns)
            if (isPunct(a)) failAt(peek().line, "assignment inside an expression not supported");
        return e;
    }

    int newTemp() { return nextVar++; }

    static ExprPtr truth(ExprPtr e) {
        if (isBoolKind(e->kind)) return e;
        return makeBinOp(ExprKind::Ne, std::move(e), makeIntConst(0));
    }

    // Lowers a C expression to a pure Mini-C expression, appending to `pre`
    // the statements that must run first (short-circuit and ?: temporaries).
    ExprPtr lower(const CExpr& e, std::vector<StmtPtr>& pre) {
        bool isUnsigned = false;
        return lower(e, pre, isUnsigned);
    }

    // The same, also giving the expression's C type: int or unsigned (both
    // 32 bits). Mini-C values are the int with the same bits; the type only
    // changes ordered comparisons, `>>`, `/` and `%`.
    ExprPtr lower(const CExpr& e, std::vector<StmtPtr>& pre, bool& isUnsigned) {
        isUnsigned = false;
        if (e.op == "num") {
            isUnsigned = e.isUnsigned;
            if (!e.isUnsigned && e.value > INT_MAX) failAt(e.line, "integer literal does not fit in int");
            return makeIntConst(static_cast<int>(static_cast<uint32_t>(e.value)));
        }
        if (e.op == "id") {
            int v = lookup(e.name, e.line);
            isUnsigned = varType(v).promotedUnsigned();
            return makeVar(v);
        }
        if (e.op == "call") return inlineCall(e, pre, isUnsigned);
        if (e.op == "index") return makeArrayAccess(lower(*e.kids[0], pre));
        if (e.op == "cast") {
            ExprPtr x = convert(lower(*e.kids[0], pre), e.type);
            isUnsigned = e.type.promotedUnsigned();
            return x;
        }
        if (e.op == "neg") return makeBinOp(ExprKind::Sub, makeIntConst(0), lower(*e.kids[0], pre, isUnsigned));
        if (e.op == "~") return makeBinOp(ExprKind::BitXor, lower(*e.kids[0], pre, isUnsigned), makeIntConst(-1));
        if (e.op == "!") return makeBinOp(ExprKind::Eq, lower(*e.kids[0], pre), makeIntConst(0));
        if (e.op == "&&" || e.op == "||") {
            bool isAnd = e.op == "&&";
            ExprPtr a = truth(lower(*e.kids[0], pre));
            if (cannotFail(*e.kids[1])) {
                std::vector<StmtPtr> inner;
                ExprPtr b = truth(lower(*e.kids[1], inner));
                if (inner.empty())
                    return makeBinOp(isAnd ? ExprKind::BitAnd : ExprKind::BitOr, std::move(a), std::move(b));
            }
            int t = newTemp();
            pre.push_back(makeAssign(t, std::move(a)));
            std::vector<StmtPtr> body;
            ExprPtr b = truth(lower(*e.kids[1], body));
            body.push_back(makeAssign(t, std::move(b)));
            ExprPtr guard = isAnd ? makeVar(t) : makeBinOp(ExprKind::Eq, makeVar(t), makeIntConst(0));
            pre.push_back(makeIf(std::move(guard), std::move(body)));
            return makeVar(t);
        }
        if (e.op == "?:") {
            ExprPtr c = lower(*e.kids[0], pre);
            int t = newTemp();
            std::vector<StmtPtr> thenBody, elseBody;
            bool ux = false, uy = false;
            ExprPtr x = lower(*e.kids[1], thenBody, ux);
            thenBody.push_back(makeAssign(t, std::move(x)));
            ExprPtr y = lower(*e.kids[2], elseBody, uy);
            elseBody.push_back(makeAssign(t, std::move(y)));
            pre.push_back(makeIf(std::move(c), std::move(thenBody), std::move(elseBody)));
            isUnsigned = ux || uy;
            return makeVar(t);
        }
        if (binaryKinds().find(e.op) == binaryKinds().end()) failAt(e.line, "operator '" + e.op + "' not supported");
        bool ua = false, ub = false;
        ExprPtr a = lower(*e.kids[0], pre, ua);
        ExprPtr b = lower(*e.kids[1], pre, ub);
        return binary(e.op, std::move(a), ua, std::move(b), ub, e.line, pre, isUnsigned);
    }

    static ExprPtr flipSign(ExprPtr x) {
        if (x->kind == ExprKind::IntConst) return makeIntConst(x->intValue ^ INT_MIN);
        return makeBinOp(ExprKind::BitXor, std::move(x), makeIntConst(INT_MIN));
    }

    // A pure operand used twice: a variable or constant as is, anything else
    // through a temporary (evaluated once, in order).
    ExprPtr reusable(ExprPtr x, std::vector<StmtPtr>& pre) {
        if (x->kind == ExprKind::Var || x->kind == ExprKind::IntConst) return x;
        int t = newTemp();
        pre.push_back(makeAssign(t, std::move(x)));
        return makeVar(t);
    }

    static ExprPtr unsignedGe(ExprPtr a, ExprPtr b) {
        return makeBinOp(ExprKind::Ge, flipSign(std::move(a)), flipSign(std::move(b)));
    }

    // Unsigned `a / b` or `a % b` with signed Mini-C division only (Hacker's
    // Delight 9-3, log 196). For a below 2^31 and b in 1..2^31-1, signed
    // division is already right. A divisor of 2^31 or more makes the quotient
    // a >= b (0 or 1). Otherwise, with a at or above 2^31, q = ((a >>> 1) / b) << 1
    // is the quotient or one less: one more when a - q*b is still >= b. The
    // remainder is a - q*b. A zero divisor still fails, as in C.
    ExprPtr unsignedDivide(bool divide, ExprPtr a, ExprPtr b, std::vector<StmtPtr>& pre) {
        if (b->kind == ExprKind::IntConst) {
            uint32_t d = static_cast<uint32_t>(b->intValue);
            if (d != 0 && (d & (d - 1)) == 0) {  // a power of two
                if (!divide) return makeBinOp(ExprKind::BitAnd, std::move(a), makeIntConst(static_cast<int>(d - 1)));
                int n = 0;
                while ((1u << n) != d) ++n;
                bool unused = false;
                return binary(">>", std::move(a), true, makeIntConst(n), false, 0, pre, unused);
            }
            if (d == 0) return makeBinOp(divide ? ExprKind::Div : ExprKind::Mod, std::move(a), std::move(b));
        }
        ExprPtr x = reusable(std::move(a), pre);
        ExprPtr y = reusable(std::move(b), pre);
        int q = newTemp();
        auto halfQuotient = [&] {
            // q = ((x >>> 1) / y) << 1; if (x - q*y >= y) q = q + 1;
            std::vector<StmtPtr> body;
            ExprPtr half = makeBinOp(ExprKind::BitAnd, makeBinOp(ExprKind::Shr, x->clone(), makeIntConst(1)),
                                     makeIntConst(INT_MAX));
            body.push_back(makeAssign(q, makeBinOp(ExprKind::Shl, makeBinOp(ExprKind::Div, std::move(half), y->clone()),
                                                   makeIntConst(1))));
            ExprPtr rest = makeBinOp(ExprKind::Sub, x->clone(), makeBinOp(ExprKind::Mul, makeVar(q), y->clone()));
            std::vector<StmtPtr> bump;
            bump.push_back(makeAssign(q, makeBinOp(ExprKind::Add, makeVar(q), makeIntConst(1))));
            body.push_back(makeIf(unsignedGe(std::move(rest), y->clone()), std::move(bump)));
            return body;
        };
        std::vector<StmtPtr> small, large;
        small.push_back(makeAssign(q, makeBinOp(ExprKind::Div, x->clone(), y->clone())));
        large = halfQuotient();
        StmtPtr byDividend = makeIf(makeBinOp(ExprKind::Ge, x->clone(), makeIntConst(0)), std::move(small),
                                    std::move(large));
        bool constantDivisor = y->kind == ExprKind::IntConst;
        if (constantDivisor && y->intValue < 0) {
            pre.push_back(makeAssign(q, unsignedGe(x->clone(), y->clone())));
        } else if (constantDivisor) {
            pre.push_back(std::move(byDividend));
        } else {
            std::vector<StmtPtr> huge, rest;
            huge.push_back(makeAssign(q, unsignedGe(x->clone(), y->clone())));
            rest.push_back(std::move(byDividend));
            pre.push_back(makeIf(makeBinOp(ExprKind::Lt, y->clone(), makeIntConst(0)), std::move(huge), std::move(rest)));
        }
        if (divide) return makeVar(q);
        return makeBinOp(ExprKind::Sub, std::move(x), makeBinOp(ExprKind::Mul, makeVar(q), std::move(y)));
    }

    // `a op b` under C's usual arithmetic conversions for int / unsigned.
    ExprPtr binary(const std::string& op, ExprPtr a, bool ua, ExprPtr b, bool ub, int /*line*/,
                   std::vector<StmtPtr>& pre, bool& isUnsigned) {
        ExprKind k = binaryKinds().at(op);
        bool shift = k == ExprKind::Shl || k == ExprKind::Shr;
        bool u = shift ? ua : (ua || ub);  // a shift has its left operand's type
        isUnsigned = u && !isBoolKind(k);
        if (!u) return makeBinOp(k, std::move(a), std::move(b));
        switch (k) {
            case ExprKind::Lt:
            case ExprKind::Gt:
            case ExprKind::Le:
            case ExprKind::Ge:
                // Unsigned order is signed order with the sign bit flipped.
                return makeBinOp(k, flipSign(std::move(a)), flipSign(std::move(b)));
            case ExprKind::Shr: {
                // Logical shift: the arithmetic one with the copied sign bits
                // cleared, mask = ~((INT_MIN >> b) << 1). A count outside 0..31
                // still fails, as it is undefined in C.
                if (b->kind == ExprKind::IntConst && b->intValue >= 0 && b->intValue < 32) {
                    int n = b->intValue;
                    if (n == 0) return a;
                    int mask = static_cast<int>(0xFFFFFFFFu >> n);
                    return makeBinOp(ExprKind::BitAnd, makeBinOp(ExprKind::Shr, std::move(a), std::move(b)),
                                     makeIntConst(mask));
                }
                ExprPtr n = reusable(std::move(b), pre);
                ExprPtr high = makeBinOp(ExprKind::Shl, makeBinOp(ExprKind::Shr, makeIntConst(INT_MIN), n->clone()),
                                         makeIntConst(1));
                return makeBinOp(ExprKind::BitAnd, makeBinOp(ExprKind::Shr, std::move(a), std::move(n)),
                                 makeBinOp(ExprKind::BitXor, std::move(high), makeIntConst(-1)));
            }
            case ExprKind::Div:
            case ExprKind::Mod:
                return unsignedDivide(k == ExprKind::Div, std::move(a), std::move(b), pre);
            default:
                // + - * << & | ^ == != : the same bits as for int.
                return makeBinOp(k, std::move(a), std::move(b));
        }
    }

    ExprPtr inlineCall(const CExpr& e, std::vector<StmtPtr>& pre, bool& isUnsigned) {
        auto h = helpers.find(e.name);
        if (h == helpers.end()) failAt(e.line, "call to '" + e.name + "': not a function defined in this file");
        if (!h->second.error.empty()) failAt(e.line, "cannot inline '" + e.name + "': " + h->second.error);
        if (std::find(inlining.begin(), inlining.end(), e.name) != inlining.end())
            failAt(e.line, "recursive helper '" + e.name + "' not supported");
        if (e.kids.size() != h->second.params.size())
            failAt(e.line, "'" + e.name + "' takes " + std::to_string(h->second.params.size()) + " arguments");
        std::map<std::string, int> bound;
        for (size_t k = 0; k < e.kids.size(); ++k) {
            bool argUnsigned = false;
            ExprPtr v = lower(*e.kids[k], pre, argUnsigned);
            const CType& ptype = h->second.paramTypes[k];
            int t;
            // An argument variable is used directly (temporaries never write
            // user variables) when its value already has the parameter's type.
            if (v->kind == ExprKind::Var &&
                (varType(v->varIndex).same(ptype) || (!ptype.narrow() && argUnsigned == ptype.isUnsigned))) {
                t = v->varIndex;
            } else {
                t = newTemp();
                pre.push_back(makeAssign(t, convert(std::move(v), ptype)));
                if (ptype.narrow() || ptype.isUnsigned) varTypes[t] = ptype;
            }
            bound[h->second.params[k]] = t;
        }
        auto saved = std::move(scopes);
        scopes = {bound};
        inlining.push_back(e.name);
        ExprPtr r = convert(lower(*h->second.expr, pre), h->second.returnType);
        inlining.pop_back();
        scopes = std::move(saved);
        isUnsigned = h->second.returnType.promotedUnsigned();
        return r;
    }

    // ------------------------------------------------------- statements

    // A loop body (see LoopCtx): translated directly, or wrapped when a
    // `continue` must still run the step / condition.
    std::vector<StmtPtr> loopBody(bool direct) {
        size_t save = pos;
        int saveVar = nextVar;
        std::map<int, CType> saveTypes = varTypes;
        loops.push_back({direct});
        auto b = body();
        LoopCtx c = loops.back();
        loops.pop_back();
        if (!c.needsWrap) return b;
        pos = save;
        nextVar = saveVar;
        varTypes = saveTypes;
        int flag = newTemp();
        loops.push_back({false, true, flag});
        b = body();
        c = loops.back();
        loops.pop_back();
        std::vector<StmtPtr> out;
        if (c.usedBreak) out.push_back(makeAssign(flag, makeIntConst(0)));
        b.push_back(makeBreak());
        out.push_back(makeWhile(makeIntConst(1), std::move(b)));
        if (c.usedBreak) {
            std::vector<StmtPtr> brk;
            brk.push_back(makeBreak());
            out.push_back(makeIf(makeVar(flag), std::move(brk)));
        }
        return out;
    }

    void appendAll(std::vector<StmtPtr>& out, std::vector<StmtPtr>& more) {
        for (auto& s : more) out.push_back(std::move(s));
        more.clear();
    }

    // A statement, or a braced block, as a list (a single statement body
    // opens its own scope too).
    std::vector<StmtPtr> body() {
        std::vector<StmtPtr> out;
        scopes.emplace_back();
        if (eat("{")) {
            while (!eat("}")) {
                if (peek().kind == Tok::End) failAt(peek().line, "unexpected end of file");
                statement(out);
            }
        } else {
            statement(out);
        }
        scopes.pop_back();
        return out;
    }

    // `x = e`, `x op= e`, `x++`, `++x`, `a[i] = e`, ... without the `;`.
    void simpleStatement(std::vector<StmtPtr>& out) {
        int line = peek().line;
        std::string prefix;
        if (isPunct("++") || isPunct("--")) prefix = toks[pos++].text;
        if (peek().kind != Tok::Ident) failAt(line, "expected an assignment, found '" + peek().text + "'");
        std::string name = ident();
        CExprPtr index;
        if (eat("[")) {
            if (name != arrayName) failAt(line, "'" + name + "' is not the array parameter");
            index = parseExpr();
            expect("]");
        }
        if (isPunct("(")) failAt(line, "a call as a statement is not supported ('" + name + "'): its value is unused");
        std::string op;  // "=" or the binary operator of a compound/increment
        bool increment = false;
        if (!prefix.empty()) {
            op = prefix == "++" ? "+" : "-";
            increment = true;
        } else if (isPunct("++") || isPunct("--")) {
            op = toks[pos++].text == "++" ? "+" : "-";
            increment = true;
        } else if (eat("=")) {
            op = "=";
        } else {
            static const char* compound[] = {"+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="};
            for (const char* c : compound)
                if (op.empty() && isPunct(c)) {
                    op = std::string(c).substr(0, std::char_traits<char>::length(c) - 1);
                    ++pos;
                }
            if (op.empty()) failAt(line, "expression statement without an assignment not supported");
        }
        std::vector<StmtPtr> pre;
        ExprPtr ix = index ? lower(*index, pre) : nullptr;
        int var = index ? -1 : lookup(name, line);
        ExprPtr value;
        if (op == "=") {
            value = lower(*parseExpr(), pre);
            if (isPunct("=")) failAt(line, "chained assignment not supported");
        } else {
            ExprPtr cur = index ? makeArrayAccess(ix->clone()) : makeVar(var);
            bool curUnsigned = !index && varType(var).promotedUnsigned(), operandUnsigned = false, unused = false;
            ExprPtr operand = increment ? makeIntConst(1) : lower(*parseExpr(), pre, operandUnsigned);
            value = binary(op, std::move(cur), curUnsigned, std::move(operand), operandUnsigned, line, pre, unused);
        }
        appendAll(out, pre);
        if (index) {
            out.push_back(makeAssignArray(std::move(ix), std::move(value)));
        } else {
            out.push_back(makeAssign(var, convert(std::move(value), varType(var))));
        }
    }

    void declaration(std::vector<StmtPtr>& out) {
        CType type = intType();
        for (;;) {
            int line = peek().line;
            if (isPunct("*")) failAt(line, "local pointers not supported");
            std::string name = ident();
            if (isPunct("[")) failAt(line, "local arrays not supported");
            int v = nextVar++;
            if (type.narrow() || type.isUnsigned) varTypes[v] = type;
            if (eat("=")) {
                std::vector<StmtPtr> pre;
                ExprPtr init = convert(lower(*parseExpr(), pre), type);
                appendAll(out, pre);
                out.push_back(makeAssign(v, std::move(init)));
            }
            // Declared after the initializer, as in C: `int x = x;` reads an outer x.
            scopes.back()[name] = v;
            if (eat(";")) return;
            expect(",");
        }
    }

    // A loop condition: `pre` (if any) is re-run before each test.
    void loopCondition(const CExpr* cond, std::vector<StmtPtr>& pre, ExprPtr& test) {
        if (cond == nullptr) {
            test = makeIntConst(1);
            return;
        }
        test = lower(*cond, pre);
    }

    StmtPtr breakUnless(ExprPtr test) {
        std::vector<StmtPtr> b;
        b.push_back(makeBreak());
        return makeIf(makeBinOp(ExprKind::Eq, std::move(test), makeIntConst(0)), std::move(b));
    }

    void statement(std::vector<StmtPtr>& out) {
        rejectOtherTypes();
        int line = peek().line;
        if (isPunct("{")) {
            auto b = body();
            appendAll(out, b);
            return;
        }
        if (eat(";")) return;
        if (atIntType()) {
            declaration(out);
            return;
        }
        if (isWord("if")) {
            ++pos;
            expect("(");
            CExprPtr c = parseExpr();
            expect(")");
            std::vector<StmtPtr> pre;
            ExprPtr test = lower(*c, pre);
            auto thenBody = body();
            std::vector<StmtPtr> elseBody;
            if (isWord("else")) {
                ++pos;
                elseBody = body();
            }
            appendAll(out, pre);
            out.push_back(makeIf(std::move(test), std::move(thenBody), std::move(elseBody)));
            return;
        }
        if (isWord("while")) {
            ++pos;
            expect("(");
            CExprPtr c = parseExpr();
            expect(")");
            std::vector<StmtPtr> pre;
            ExprPtr test;
            loopCondition(c.get(), pre, test);
            auto loopBody = this->loopBody(true);
            if (pre.empty()) {
                out.push_back(makeWhile(std::move(test), std::move(loopBody)));
            } else {
                // `continue` lands on while(1)'s check, which re-runs `pre`.
                pre.push_back(breakUnless(std::move(test)));
                appendAll(pre, loopBody);
                out.push_back(makeWhile(makeIntConst(1), std::move(pre)));
            }
            return;
        }
        if (isWord("do")) {
            ++pos;
            auto loopBody = this->loopBody(false);
            if (!isWord("while")) failAt(peek().line, "expected 'while' after do body");
            ++pos;
            expect("(");
            CExprPtr c = parseExpr();
            expect(")");
            expect(";");
            std::vector<StmtPtr> pre;
            ExprPtr test = lower(*c, pre);
            appendAll(loopBody, pre);
            loopBody.push_back(breakUnless(std::move(test)));
            out.push_back(makeWhile(makeIntConst(1), std::move(loopBody)));
            return;
        }
        if (isWord("for")) {
            ++pos;
            expect("(");
            scopes.emplace_back();
            std::vector<StmtPtr> init;
            if (atIntType()) {
                declaration(init);  // consumes the ';'
            } else {
                if (!isPunct(";")) {
                    simpleStatement(init);
                    while (eat(",")) simpleStatement(init);
                }
                expect(";");
            }
            CExprPtr c;
            if (!isPunct(";")) c = parseExpr();
            expect(";");
            std::vector<StmtPtr> step;
            if (!isPunct(")")) {
                simpleStatement(step);
                while (eat(",")) simpleStatement(step);
            }
            expect(")");
            std::vector<StmtPtr> pre;
            ExprPtr test;
            loopCondition(c.get(), pre, test);
            auto loopBody = this->loopBody(step.empty());
            scopes.pop_back();
            appendAll(loopBody, step);
            appendAll(out, init);
            if (pre.empty()) {
                out.push_back(makeWhile(std::move(test), std::move(loopBody)));
            } else {
                pre.push_back(breakUnless(std::move(test)));
                appendAll(pre, loopBody);
                out.push_back(makeWhile(makeIntConst(1), std::move(pre)));
            }
            return;
        }
        if (isWord("break")) {
            ++pos;
            expect(";");
            if (loops.empty()) failAt(line, "break outside a loop");
            if (loops.back().wrapped) {
                loops.back().usedBreak = true;
                out.push_back(makeAssign(loops.back().flag, makeIntConst(1)));
            }
            out.push_back(makeBreak());
            return;
        }
        if (isWord("continue")) {
            ++pos;
            expect(";");
            if (loops.empty()) failAt(line, "continue outside a loop");
            LoopCtx& c = loops.back();
            if (c.wrapped) {
                out.push_back(makeBreak());  // leaves the inner while(1): the step runs next
            } else {
                c.needsWrap = c.needsWrap || !c.direct;
                out.push_back(makeContinue());
            }
            return;
        }
        if (isWord("return")) {
            ++pos;
            if (abi.returnsValue) {
                if (isPunct(";")) failAt(line, "return without a value in an int function");
                std::vector<StmtPtr> pre;
                ExprPtr v = convert(lower(*parseExpr(), pre), returnType);
                appendAll(out, pre);
                out.push_back(makeAssignArray(makeIntConst(0), std::move(v)));
            } else if (!isPunct(";")) {
                failAt(line, "return with a value in a void function");
            }
            expect(";");
            out.push_back(makeReturn());
            return;
        }
        simpleStatement(out);
        expect(";");
    }

    // ------------------------------------------------- top level / ABI

    struct Definition {
        std::string name;
        size_t start;  // token index of the return type
        size_t end;    // one past the closing brace
    };

    // A helper `int name(int x, ...) { return expr; }`: its expression, or
    // why it cannot be inlined (recorded, reported only if it is called).
    void parseHelper(const Definition& d) {
        Helper h;
        try {
            pos = d.start;
            while (isWord("static") || isWord("inline")) ++pos;
            if (isWord("void")) failAt(peek().line, "a void helper has no value to inline");
            h.returnType = intType();
            ident();
            expect("(");
            if (isWord("void") && isPunct(")", 1)) ++pos;
            while (!eat(")")) {
                if (!h.params.empty()) expect(",");
                rejectOtherTypes();
                h.paramTypes.push_back(intType());
                if (isPunct("*") || isPunct("[", 1)) failAt(peek().line, "helpers take int scalars only");
                h.params.push_back(ident());
            }
            expect("{");
            if (!isWord("return")) failAt(peek().line, "only helpers whose body is `return expr;` are inlined");
            ++pos;
            h.expr = parseExpr();
            expect(";");
            if (!isPunct("}")) failAt(peek().line, "only helpers whose body is `return expr;` are inlined");
        } catch (const CImportError& err) {
            h.error = err.what();
            h.expr = nullptr;
        }
        helpers[d.name] = std::move(h);
    }

    std::vector<Definition> findDefinitions() {
        std::vector<Definition> defs;
        while (peek().kind != Tok::End) {
            size_t start = pos;
            while (isWord("static") || isWord("inline")) ++pos;
            rejectOtherTypes();
            if (isWord("void"))
                ++pos;
            else if (atIntType())
                intType();
            else
                failAt(peek().line, "expected a function definition, found '" + peek().text + "'");
            std::string name = ident();
            if (!isPunct("(")) failAt(peek().line, "only function definitions are supported at top level");
            int depth = 0;
            do {
                if (peek().kind == Tok::End) failAt(peek().line, "unexpected end of file");
                if (isPunct("(")) ++depth;
                if (isPunct(")")) --depth;
                ++pos;
            } while (depth > 0);
            if (eat(";")) continue;  // prototype
            if (!isPunct("{")) failAt(peek().line, "expected a function body");
            depth = 0;
            do {
                if (peek().kind == Tok::End) failAt(peek().line, "unexpected end of file");
                if (isPunct("{")) ++depth;
                if (isPunct("}")) --depth;
                ++pos;
            } while (depth > 0);
            defs.push_back({name, start, pos});
        }
        return defs;
    }

    std::vector<StmtPtr> signature() {
        while (isWord("static") || isWord("inline")) ++pos;
        abi.returnsValue = !isWord("void");
        if (abi.returnsValue) {
            returnType = intType();
            abi.returnType = returnType.spelling;
        } else {
            ++pos;
        }
        abi.name = ident();
        expect("(");
        struct Param {
            std::string name;
            bool pointer;
            int line;
            CType type;
        };
        std::vector<Param> params;
        if (isWord("void") && isPunct(")", 1)) ++pos;
        while (!eat(")")) {
            if (!params.empty()) expect(",");
            int line = peek().line;
            rejectOtherTypes();
            CType type = intType();
            bool pointer = eat("*");
            if (pointer && isWord("const")) ++pos;
            if (pointer && (type.narrow() || type.isUnsigned)) failAt(line, "the array parameter must be int *");
            Param p{ident(), pointer, line, type};
            if (eat("[")) {
                expect("]");
                p.pointer = true;
            }
            params.push_back(p);
        }
        std::vector<StmtPtr> prologue;
        scopes.emplace_back();
        bool anyPointer = false;
        for (const auto& p : params) anyPointer = anyPointer || p.pointer;
        if (anyPointer) {
            if (params.size() != 2 || !params[0].pointer || params[1].pointer)
                failAt(params[0].line, "array functions must have the form f(int *a, int n)");
            abi.kind = CFunctionAbi::Kind::Array;
            arrayName = params[0].name;
            scopes.back()[params[1].name] = 0;
            if (params[1].type.narrow()) failAt(params[1].line, "the length parameter must be int or unsigned");
            if (params[1].type.isUnsigned) varTypes[0] = params[1].type;
        } else {
            if (params.empty()) failAt(peek().line, "functions without parameters not supported");
            if (!abi.returnsValue) failAt(peek().line, "a void function of scalars has no observable result");
            abi.kind = CFunctionAbi::Kind::Scalars;
            for (size_t i = 0; i < params.size(); ++i) {
                int v = nextVar++;
                scopes.back()[params[i].name] = v;
                if (params[i].type.narrow() || params[i].type.isUnsigned) varTypes[v] = params[i].type;
                // The caller converts the argument to the parameter's type.
                prologue.push_back(
                    makeAssign(v, convert(makeArrayAccess(makeIntConst(static_cast<int>(i))), params[i].type)));
            }
        }
        for (const auto& p : params) {
            abi.params.push_back(p.name);
            abi.paramTypes.push_back(p.type.spelling);
        }
        return prologue;
    }
};

void renameVar(Expr& e, int from, int to) {
    if (e.kind == ExprKind::Var && e.varIndex == from) e.varIndex = to;
    if (e.lhs) renameVar(*e.lhs, from, to);
    if (e.rhs) renameVar(*e.rhs, from, to);
}

void renameVar(Stmt& s, int from, int to) {
    if (s.kind == StmtKind::Assign && s.varIndex == from) s.varIndex = to;
    for (Expr* e : {s.valueExpr.get(), s.indexExpr.get(), s.indexExpr2.get(), s.cond.get()})
        if (e) renameVar(*e, from, to);
    for (auto& b : s.body) renameVar(*b, from, to);
    for (auto& b : s.elseBody) renameVar(*b, from, to);
}

bool writesVar(const Stmt& s, int v) {
    if (s.kind == StmtKind::Assign && s.varIndex == v) return true;
    for (const auto& b : s.body)
        if (writesVar(*b, v)) return true;
    for (const auto& b : s.elseBody)
        if (writesVar(*b, v)) return true;
    return false;
}

bool usesStmt(const Stmt& s, StmtKind kind) {
    if (s.kind == kind) return true;
    for (const auto& b : s.body)
        if (usesStmt(*b, kind)) return true;
    for (const auto& b : s.elseBody)
        if (usesStmt(*b, kind)) return true;
    return false;
}

bool usesAuxExpr(const Expr* e) {
    if (!e) return false;
    return e->kind == ExprKind::AuxAccess || usesAuxExpr(e->lhs.get()) || usesAuxExpr(e->rhs.get());
}

bool usesAux(const Stmt& s) {
    if (s.kind == StmtKind::AssignAux) return true;
    for (const Expr* e : {s.valueExpr.get(), s.indexExpr.get(), s.indexExpr2.get(), s.cond.get()})
        if (usesAuxExpr(e)) return true;
    for (const auto& b : s.body)
        if (usesAux(*b)) return true;
    for (const auto& b : s.elseBody)
        if (usesAux(*b)) return true;
    return false;
}

int maxVar(const Expr* e) {
    if (!e) return -1;
    int m = e->kind == ExprKind::Var ? e->varIndex : -1;
    return std::max({m, maxVar(e->lhs.get()), maxVar(e->rhs.get())});
}

int maxVar(const Stmt& s) {
    int m = s.kind == StmtKind::Assign ? s.varIndex : -1;
    for (const Expr* e : {s.valueExpr.get(), s.indexExpr.get(), s.indexExpr2.get(), s.cond.get()})
        m = std::max(m, maxVar(e));
    for (const auto& b : s.body) m = std::max(m, maxVar(*b));
    for (const auto& b : s.elseBody) m = std::max(m, maxVar(*b));
    return m;
}

// ---------------------------------------------------------------- emitter

std::string cExpr(const Expr& e, const std::string& arrName) {
    switch (e.kind) {
        case ExprKind::IntConst:
            if (e.intValue == INT_MIN) return "(-2147483647 - 1)";
            return e.intValue < 0 ? "(" + std::to_string(e.intValue) + ")" : std::to_string(e.intValue);
        case ExprKind::Var:
            return "v" + std::to_string(e.varIndex);
        case ExprKind::ArrayAccess:
            return arrName + "[" + cExpr(*e.lhs, arrName) + "]";
        case ExprKind::AuxAccess:
            return "aux[" + cExpr(*e.lhs, arrName) + "]";
        case ExprKind::Shl:
            return "((int)((unsigned)" + cExpr(*e.lhs, arrName) + " << " + cExpr(*e.rhs, arrName) + "))";
        default:
            break;
    }
    static const std::map<ExprKind, const char*> sym = {
        {ExprKind::Add, "+"},  {ExprKind::Sub, "-"},     {ExprKind::Mul, "*"},    {ExprKind::Div, "/"},
        {ExprKind::Mod, "%"},  {ExprKind::BitAnd, "&"},  {ExprKind::BitOr, "|"},  {ExprKind::BitXor, "^"},
        {ExprKind::Shr, ">>"}, {ExprKind::Lt, "<"},      {ExprKind::Gt, ">"},     {ExprKind::Le, "<="},
        {ExprKind::Ge, ">="},  {ExprKind::Eq, "=="},     {ExprKind::Ne, "!="}};
    return "(" + cExpr(*e.lhs, arrName) + " " + sym.at(e.kind) + " " + cExpr(*e.rhs, arrName) + ")";
}

struct Emitter {
    std::ostringstream out;
    const CFunctionAbi& abi;
    std::string arrName;
    // Print `arr[0] = E; return;` as `return E;` (an int array function whose
    // only array write is its result, so the caller's array stays untouched).
    bool liftReturn = false;

    explicit Emitter(const CFunctionAbi& a) : abi(a) {}

    static bool isResultWrite(const Stmt& s) {
        return s.kind == StmtKind::AssignArray && s.indexExpr->kind == ExprKind::IntConst &&
               s.indexExpr->intValue == 0;
    }

    // Returns whether the list ends in a return (so nothing after it runs).
    // `topLevel`: the function body, whose last statement falls through to
    // the implicit `return arr[0]`, so a result write there is a return too.
    bool list(const std::vector<StmtPtr>& b, int indent, bool topLevel = false) {
        bool returned = false;
        for (size_t i = 0; i < b.size(); ++i) {
            if (liftReturn && topLevel && i + 1 == b.size() && isResultWrite(*b[i])) {
                out << std::string(static_cast<size_t>(indent) * 4, ' ') << "return " << cExpr(*b[i]->valueExpr, arrName)
                    << ";\n";
                return true;
            }
            if (liftReturn && i + 1 < b.size() && isResultWrite(*b[i]) && b[i + 1]->kind == StmtKind::Return) {
                out << std::string(static_cast<size_t>(indent) * 4, ' ') << "return " << cExpr(*b[i]->valueExpr, arrName)
                    << ";\n";
                ++i;
                returned = true;
                continue;
            }
            returned = stmt(*b[i], indent);
        }
        return returned;
    }

    bool stmt(const Stmt& s, int indent) {
        std::string pad(static_cast<size_t>(indent) * 4, ' ');
        switch (s.kind) {
            case StmtKind::Block:
                return list(s.body, indent);
            case StmtKind::Assign:
                out << pad << "v" << s.varIndex << " = " << cExpr(*s.valueExpr, arrName) << ";\n";
                return false;
            case StmtKind::AssignArray:
                out << pad << arrName << "[" << cExpr(*s.indexExpr, arrName) << "] = " << cExpr(*s.valueExpr, arrName)
                    << ";\n";
                return false;
            case StmtKind::AssignAux:
                out << pad << "aux[" << cExpr(*s.indexExpr, arrName) << "] = " << cExpr(*s.valueExpr, arrName)
                    << ";\n";
                return false;
            case StmtKind::If:
                out << pad << "if (" << cExpr(*s.cond, arrName) << ") {\n";
                list(s.body, indent + 1);
                if (!s.elseBody.empty()) {
                    out << pad << "} else {\n";
                    list(s.elseBody, indent + 1);
                }
                out << pad << "}\n";
                return false;
            case StmtKind::While:
                out << pad << "while (" << cExpr(*s.cond, arrName) << ") {\n";
                list(s.body, indent + 1);
                out << pad << "}\n";
                return false;
            case StmtKind::Swap:
                out << pad << "{ int i_ = " << cExpr(*s.indexExpr, arrName) << ", j_ = "
                    << cExpr(*s.indexExpr2, arrName) << "; int t_ = " << arrName << "[i_]; " << arrName
                    << "[i_] = " << arrName << "[j_]; " << arrName << "[j_] = t_; }\n";
                return false;
            case StmtKind::Return:
                out << pad << (abi.returnsValue ? "return " + arrName + "[0];\n" : std::string("return;\n"));
                return true;
            case StmtKind::Continue:
                out << pad << "continue;\n";
                return false;
            case StmtKind::Break:
                out << pad << "break;\n";
                return false;
            case StmtKind::Call:
                break;
        }
        throw CImportError("emitting call() (recursion) as C is not supported yet");
    }
};

// Whether the program writes the array anywhere other than a result write
// immediately followed by return (or, at top level, ending the function).
bool writesArrayBeyondResult(const std::vector<StmtPtr>& b, bool topLevel = false) {
    for (size_t i = 0; i < b.size(); ++i) {
        const Stmt& s = *b[i];
        if (Emitter::isResultWrite(s) && i + 1 < b.size() && b[i + 1]->kind == StmtKind::Return) {
            ++i;
            continue;
        }
        if (topLevel && i + 1 == b.size() && Emitter::isResultWrite(s)) continue;
        if (s.kind == StmtKind::AssignArray || s.kind == StmtKind::Swap) return true;
        if (writesArrayBeyondResult(s.body) || writesArrayBeyondResult(s.elseBody)) return true;
    }
    return false;
}

bool reservedName(const std::string& n) {
    static const std::regex r("^(v[0-9]+|arr|aux|i_|j_|t_|k_)$");
    return std::regex_match(n, r);
}

}  // namespace

CImport importCFunction(const std::string& source, const std::string& funcName) {
    Translator t;
    t.toks = lex(source);
    auto defs = t.findDefinitions();
    const Translator::Definition* chosen = nullptr;
    std::string names;
    // Without --func: the only function, or else the only one no other
    // function of the file calls (the helpers are the called ones).
    std::vector<const Translator::Definition*> uncalled;
    for (const auto& d : defs) {
        bool called = false;
        for (const auto& o : defs)
            for (size_t k = o.start; &o != &d && k + 1 < o.end && !called; ++k)
                called = t.toks[k].kind == Tok::Ident && t.toks[k].text == d.name && t.toks[k + 1].text == "(";
        if (!called) uncalled.push_back(&d);
    }
    for (const auto& d : defs) {
        names += (names.empty() ? "" : ", ") + d.name;
        if (funcName.empty() ? defs.size() == 1 : d.name == funcName) chosen = &d;
    }
    if (chosen == nullptr && funcName.empty() && uncalled.size() == 1) chosen = uncalled[0];
    if (chosen == nullptr) {
        if (defs.empty()) throw CImportError("no function definition found");
        throw CImportError(funcName.empty() ? "several functions (" + names + "): choose one with --func"
                                            : "function '" + funcName + "' not found (have: " + names + ")");
    }
    for (const auto& d : defs)
        if (&d != chosen) t.parseHelper(d);
    t.pos = chosen->start;
    std::vector<StmtPtr> prologue = t.signature();
    std::vector<StmtPtr> body = t.body();

    Program program = makeBlock();
    for (auto& s : prologue) program->body.push_back(std::move(s));
    for (auto& s : body) program->body.push_back(std::move(s));

    // v0 is the array length register; a function that writes its length
    // parameter gets a private copy instead.
    if (t.abi.kind == CFunctionAbi::Kind::Array && writesVar(*program, 0)) {
        int copy = t.nextVar++;
        renameVar(*program, 0, copy);
        if (t.varTypes.count(0)) t.varTypes[copy] = t.varTypes[0];
        program->body.insert(program->body.begin(), makeAssign(copy, makeVar(0)));
    }
    t.abi.numVars = std::max(t.nextVar, maxVar(*program) + 1);
    return {std::move(program), t.abi};
}

std::string emitCFunction(const Stmt& program, const CFunctionAbi& abi) {
    if (usesStmt(program, StmtKind::Call)) throw CImportError("emitting call() (recursion) as C is not supported yet");
    std::vector<std::string> names = abi.params;
    for (size_t i = 0; i < names.size(); ++i)
        if (reservedName(names[i])) names[i] = "p" + std::to_string(i) + "_";
    int numVars = std::max(abi.numVars, maxVar(program) + 1);

    Emitter em(abi);
    std::ostringstream& out = em.out;
    out << "/* Generated by algosearch from Mini-C; compile with -fwrapv. */\n";
    // The original's signature; inside, every value is an int with the same
    // bits (gcc converts unsigned <-> int modulo 2^32).
    auto type = [&](size_t i) { return (i < abi.paramTypes.size() ? abi.paramTypes[i] : std::string("int")) + " "; };
    out << (!abi.returnsValue ? std::string("void") : abi.returnType) << " " << abi.name << "(";
    if (abi.kind == CFunctionAbi::Kind::Array) {
        out << "int *" << names[0] << ", " << type(1) << names[1] << ") {\n";
        out << "    int v0 = " << names[1] << ";\n";
        em.arrName = names[0];
        if (abi.returnsValue) {
            if (writesArrayBeyondResult(program.body, /*topLevel=*/true)) {
                // The Mini-C program uses the array as scratch space; the C
                // caller's array must stay as it was, so work on a copy.
                out << "    int arr[v0 > 0 ? v0 : 1];\n";
                out << "    for (int k_ = 0; k_ < v0; k_++) arr[k_] = " << names[0] << "[k_];\n";
                em.arrName = "arr";
            } else {
                em.liftReturn = true;
            }
        }
    } else {
        for (size_t i = 0; i < names.size(); ++i) out << (i ? ", " : "") << type(i) << names[i];
        out << ") {\n";
        out << "    int arr[" << names.size() << "] = {";
        for (size_t i = 0; i < names.size(); ++i) out << (i ? ", " : "") << names[i];
        out << "};\n";
        out << "    int v0 = " << names.size() << ";\n";
        em.arrName = "arr";
        em.liftReturn = true;
    }
    if (numVars > 1) {
        out << "    int";
        for (int v = 1; v < numVars; ++v) out << (v > 1 ? "," : "") << " v" << v << " = 0";
        out << ";\n";
    }
    out << "    (void)v0;\n";
    if (usesAux(program)) {
        out << "    int aux[v0 > 0 ? v0 : 1];\n";
        out << "    for (int k_ = 0; k_ < v0; k_++) aux[k_] = 0;\n";
    }
    bool returned = em.list(program.body, 1, /*topLevel=*/true);
    if (!returned && abi.returnsValue) out << "    return " << em.arrName << "[0];\n";
    out << "}\n";
    return out.str();
}

bool emittedCopiesArray(const Stmt& program, const CFunctionAbi& abi) {
    return abi.kind == CFunctionAbi::Kind::Array && abi.returnsValue &&
           writesArrayBeyondResult(program.body, /*topLevel=*/true);
}

std::vector<int> abiResult(const CFunctionAbi& abi, const std::vector<int>& finalArray) {
    if (abi.returnsValue) {
        if (finalArray.empty()) return {};
        return {finalArray[0]};
    }
    return finalArray;
}

}  // namespace algosearch
