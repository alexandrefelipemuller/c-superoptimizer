#pragma once

namespace algosearch {

// `algosearch c-import FILE [--func NAME] [--program MINIC] [--emit] [--eval INPUTS]`,
// the superoptimizer prototype's C front end (docs/superopt.md). argv[0] is
// "c-import". Prints the Mini-C translation (default), the C regenerated
// from it (--emit), or the interpreter's result for each input line of
// INPUTS (--eval; integers separated by spaces or commas, one call per
// line; prints the result values, or ERROR / TIMEOUT). --program replaces
// the translation by another Mini-C program (a search result) under FILE's ABI.
int runCImportTool(int argc, char** argv);

// `algosearch superopt FILE.c [--func NAME] [--seconds S] [--out FOUND.c] ...`:
// phase 3 (docs/superopt.md). Starts from the imported function, searches for
// a cheaper equivalent under the cost model, prints both costs and the result
// (Mini-C and C). Exit 3 if the result failed the final verification.
int runSuperoptTool(int argc, char** argv);

}  // namespace algosearch
