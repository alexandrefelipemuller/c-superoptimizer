#pragma once

namespace algosearch {

// `algosearch c-import FILE [--func NAME] [--emit] [--eval INPUTS]`, the
// superoptimizer prototype's C front end (docs/superopt.md). argv[0] is
// "c-import". Prints the Mini-C translation (default), the C regenerated
// from it (--emit), or the interpreter's result for each input line of
// INPUTS (--eval; integers separated by spaces or commas, one call per
// line; prints the result values, or ERROR / TIMEOUT).
int runCImportTool(int argc, char** argv);

}  // namespace algosearch
