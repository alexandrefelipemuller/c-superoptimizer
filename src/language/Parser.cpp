#include "language/Parser.hpp"

#include <cctype>
#include <stdexcept>

namespace algosearch {

namespace {

struct Parser {
    const std::string& s;
    size_t pos = 0;

    explicit Parser(const std::string& text) : s(text) {}

    void ws() {
        while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
    }
    [[noreturn]] void fail() { throw std::runtime_error("parse error"); }
    bool peekWord(const char* w) {
        ws();
        size_t n = std::char_traits<char>::length(w);
        if (s.compare(pos, n, w) != 0) return false;
        size_t e = pos + n;
        return e >= s.size() || !(std::isalnum(static_cast<unsigned char>(s[e])) || s[e] == '_');
    }
    bool eat(const char* t) {
        ws();
        size_t n = std::char_traits<char>::length(t);
        if (s.compare(pos, n, t) != 0) return false;
        pos += n;
        return true;
    }
    void expect(const char* t) {
        if (!eat(t)) fail();
    }

    ExprPtr expr() {
        ws();
        if (pos >= s.size()) fail();
        if (s[pos] == '(') {
            ++pos;
            ExprPtr l = expr();
            ws();
            static const struct { const char* t; ExprKind k; } ops[] = {
                {"<<", ExprKind::Shl}, {">>", ExprKind::Shr}, {"<=", ExprKind::Le}, {">=", ExprKind::Ge},
                {"==", ExprKind::Eq},  {"!=", ExprKind::Ne},  {"+", ExprKind::Add},  {"-", ExprKind::Sub},
                {"*", ExprKind::Mul},  {"/", ExprKind::Div},  {"%", ExprKind::Mod},  {"&", ExprKind::BitAnd},
                {"|", ExprKind::BitOr}, {"^", ExprKind::BitXor}, {"<", ExprKind::Lt}, {">", ExprKind::Gt}};
            for (const auto& op : ops) {
                if (eat(op.t)) {
                    ExprPtr r = expr();
                    expect(")");
                    return makeBinOp(op.k, std::move(l), std::move(r));
                }
            }
            fail();
        }
        if (s.compare(pos, 4, "arr[") == 0) {
            pos += 4;
            ExprPtr i = expr();
            expect("]");
            return makeArrayAccess(std::move(i));
        }
        if (s.compare(pos, 4, "aux[") == 0) {
            pos += 4;
            ExprPtr i = expr();
            expect("]");
            return makeAuxAccess(std::move(i));
        }
        if (s[pos] == 'v' && pos + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[pos + 1]))) {
            ++pos;
            return makeVar(number());
        }
        return makeIntConst(number());
    }

    int number() {
        ws();
        size_t st = pos;
        if (pos < s.size() && s[pos] == '-') ++pos;
        size_t d = pos;
        while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
        if (pos == d) fail();
        return std::stoi(s.substr(st, pos - st));
    }

    std::vector<StmtPtr> block() {
        expect("{");
        std::vector<StmtPtr> out = stmts();
        expect("}");
        return out;
    }

    std::vector<StmtPtr> stmts() {
        std::vector<StmtPtr> out;
        for (;;) {
            ws();
            if (pos >= s.size() || s[pos] == '}') return out;
            out.push_back(stmt());
        }
    }

    StmtPtr stmt() {
        ws();
        if (peekWord("if")) {
            pos += 2;
            expect("(");
            ExprPtr c = expr();
            expect(")");
            auto body = block();
            std::vector<StmtPtr> els;
            if (peekWord("else")) {
                pos += 4;
                els = block();
            }
            return makeIf(std::move(c), std::move(body), std::move(els));
        }
        if (peekWord("while")) {
            pos += 5;
            expect("(");
            ExprPtr c = expr();
            expect(")");
            return makeWhile(std::move(c), block());
        }
        if (peekWord("swap")) {
            pos += 4;
            expect("(arr[");
            ExprPtr a = expr();
            expect("]");
            expect(",");
            expect("arr[");
            ExprPtr b = expr();
            expect("]");
            expect(")");
            expect(";");
            return makeSwap(std::move(a), std::move(b));
        }
        if (peekWord("call")) {
            pos += 4;
            expect("(");
            ExprPtr a = expr();
            expect(",");
            ExprPtr b = expr();
            expect(")");
            expect(";");
            return makeCall(std::move(a), std::move(b));
        }
        if (peekWord("return")) { pos += 6; expect(";"); return makeReturn(); }
        if (peekWord("continue")) { pos += 8; expect(";"); return makeContinue(); }
        if (peekWord("break")) { pos += 5; expect(";"); return makeBreak(); }
        if (s.compare(pos, 4, "arr[") == 0 || s.compare(pos, 4, "aux[") == 0) {
            bool aux = s[pos] == 'a' && s[pos + 1] == 'u';
            pos += 4;
            ExprPtr i = expr();
            expect("]");
            expect("=");
            ExprPtr v = expr();
            expect(";");
            return aux ? makeAssignAux(std::move(i), std::move(v)) : makeAssignArray(std::move(i), std::move(v));
        }
        if (s[pos] == 'v') {
            ++pos;
            int idx = number();
            expect("=");
            ExprPtr v = expr();
            expect(";");
            return makeAssign(idx, std::move(v));
        }
        fail();
    }
};

}  // namespace

Program parseProgram(const std::string& text) {
    try {
        Parser p(text);
        auto root = makeBlock();
        root->body = p.stmts();
        p.ws();
        if (p.pos != text.size()) return nullptr;
        return root;
    } catch (const std::exception&) {
        return nullptr;
    }
}

}  // namespace algosearch
