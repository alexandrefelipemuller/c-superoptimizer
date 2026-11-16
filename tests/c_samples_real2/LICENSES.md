# Third-party functions in this directory

Each file is one function copied verbatim (a source comment added on its first line; `#include <stdint.h>` added to mbedtls_aes_rot_word.c) from:

- curl 8.11.0 (https://curl.se/download/curl-8.11.0.tar.gz): curl_*.c
- libpng 1.6.44 (https://download.sourceforge.net/libpng/libpng-1.6.44.tar.gz): png_*.c
- Mbed TLS 3.6.2 (https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/v3.6.2.tar.gz): mbedtls_*.c, used under Apache-2.0
- SQLite 3.46.0 amalgamation (https://www.sqlite.org/2024/sqlite-amalgamation-3460000.zip): sqlite_*.c, public domain

Picked by `scripts/c_census.py` (log 200): every function of those sources (and of zlib 1.3.1 and lz4 1.10.0, where none imports) that the superopt front end imports on its own, minus two identity stubs (`return n;`).

## curl

```
COPYRIGHT AND PERMISSION NOTICE

Copyright (c) 1996 - 2024, Daniel Stenberg, <daniel@haxx.se>, and many
contributors, see the THANKS file.

All rights reserved.

Permission to use, copy, modify, and distribute this software for any purpose
with or without fee is hereby granted, provided that the above copyright
notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS. IN
NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of a copyright holder shall not
be used in advertising or otherwise to promote the sale, use or other dealings
```

## libpng

```
PNG Reference Library License version 2
---------------------------------------

 * Copyright (c) 1995-2024 The PNG Reference Library Authors.
 * Copyright (c) 2018-2024 Cosmin Truta.
 * Copyright (c) 2000-2002, 2004, 2006-2018 Glenn Randers-Pehrson.
 * Copyright (c) 1996-1997 Andreas Dilger.
 * Copyright (c) 1995-1996 Guy Eric Schalnat, Group 42, Inc.

The software is supplied "as is", without warranty of any kind,
express or implied, including, without limitation, the warranties
of merchantability, fitness for a particular purpose, title, and
non-infringement.  In no event shall the Copyright owners, or
anyone distributing the software, be liable for any damages or
other liability, whether in contract, tort or otherwise, arising
from, out of, or in connection with the software, or the use or
other dealings in the software, even if advised of the possibility
of such damage.

Permission is hereby granted to use, copy, modify, and distribute
this software, or portions hereof, for any purpose, without fee,
subject to the following restrictions:

 1. The origin of this software must not be misrepresented; you
    must not claim that you wrote the original software.  If you
    use this software in a product, an acknowledgment in the product
    documentation would be appreciated, but is not required.

 2. Altered source versions must be plainly marked as such, and must
    not be misrepresented as being the original software.

```

## Mbed TLS

Copyright The Mbed TLS Contributors. SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later; used here under the Apache License 2.0 (https://www.apache.org/licenses/LICENSE-2.0). The files are unmodified apart from the added first-line comment and, in mbedtls_aes_rot_word.c, `#include <stdint.h>`.

## SQLite

The SQLite source code is in the public domain (https://www.sqlite.org/copyright.html).
