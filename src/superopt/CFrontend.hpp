#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#include "ast/Stmt.hpp"

namespace algosearch {

// How a C function's arguments and result map onto a Mini-C run (the
// superoptimizer prototype's ABI, see docs/superopt.md).
//
//  Scalars:  int f(int a, int b, ...)   arr = [a, b, ...], v0 = arity; the
//            prologue copies each argument into its own variable; the
//            result is arr[0].
//  Array:    int|void f(int *a, int n)  arr = a, v0 = n. An int function's
//            result is arr[0] (so n >= 1 is required and the rest of the
//            array is unspecified); a void function's result is the whole
//            final array.
struct CFunctionAbi {
    enum class Kind { Scalars, Array };
    Kind kind = Kind::Scalars;
    std::string name;
    bool returnsValue = true;
    bool returnsUnsigned = false;  // `unsigned` / `uint32_t` result (same bits as the int result)
    // Parameter names in declaration order (Array: {pointer, length}).
    std::vector<std::string> params;
    std::vector<bool> paramUnsigned;  // per parameter: declared unsigned
    // Mini-C variables the translated program uses, v0 included.
    int numVars = 1;
};

struct CImport {
    Program program;
    CFunctionAbi abi;
};

// Thrown for C outside the supported subset; the message names the
// construct and the line, never silently approximates it.
struct CImportError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Translates one function of `source` (the only one, or the one named
// `funcName`) into Mini-C. Supported subset: `int`/`int32_t` and 32-bit
// `unsigned`/`uint32_t` (no unsigned `/`, `%`) scalars and
// locals, one array parameter, if/else, while, do-while, for, break,
// continue, return, assignment and compound assignment, ++/--, the
// arithmetic, bitwise, comparison, logical (short-circuit preserved) and
// conditional operators. Everything else throws CImportError.
CImport importCFunction(const std::string& source, const std::string& funcName = "");

// Mini-C -> C: a compilable C function with the ABI's signature, intended
// to be compiled with `-fwrapv` (Mini-C Add/Sub wrap). Equivalent to the
// Mini-C program on every input on which that program completes without a
// runtime error (C has no bounds checks). Throws CImportError for Call.
std::string emitCFunction(const Stmt& program, const CFunctionAbi& abi);

// Whether emitCFunction must copy the caller's array for this program (an
// int array function that writes the array other than for its result): the
// copy is O(n) work the Mini-C cost does not see.
bool emittedCopiesArray(const Stmt& program, const CFunctionAbi& abi);

// The Mini-C input array of a call is the argument list itself (Scalars) or
// the array (Array). The call's result, read back from the final array: one
// value, or the whole array for a void array function.
std::vector<int> abiResult(const CFunctionAbi& abi, const std::vector<int>& finalArray);

}  // namespace algosearch
