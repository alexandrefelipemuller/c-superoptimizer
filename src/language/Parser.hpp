#pragma once

#include <string>

#include "ast/Stmt.hpp"

namespace algosearch {

// Parses the text printStmt() produces (fully parenthesized binary
// operators) back into a program rooted at a Block. Returns nullptr on a
// syntax error. Used to seed the beam with known programs (section 163b).
Program parseProgram(const std::string& text);

}  // namespace algosearch
