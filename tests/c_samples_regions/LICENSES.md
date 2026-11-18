# Third-party expressions in this directory

Each file is one pure scalar expression taken from a function of a third-party
code base by `scripts/c_regions.py` (log 204) and wrapped in a function
`region(...)`. Inputs that were loads (`p[i]`, `*p`) became parameters, and char
literals became their values. The first-line comment gives the source file,
line, function and original expression. File names are `<code base>_<hash>.c`.

The code bases and their licenses:
- musl 1.2.5, MIT;
- libpng 1.6.44, libpng license;
- SQLite 3.46.0, public domain;
- Mbed TLS 3.6.2, Apache-2.0;
- curl 8.11.0, curl license.

The full texts are in `tests/c_samples_musl_lua/LICENSES.md` (musl) and
`tests/c_samples_real2/LICENSES.md` (curl, libpng, Mbed TLS, SQLite).

## lz4 1.10.0 (lib/), BSD 2-Clause

```
LZ4 Library
Copyright (c) 2011-2020, Yann Collet
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice, this
  list of conditions and the following disclaimer in the documentation and/or
  other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```
