# Third-party expressions in this directory

Each file is one pure scalar expression taken by `scripts/c_regions.py` (log 205)
from the preprocessed source (`gcc -E -P`) of libpng 1.6.44 (pngwutil.c, libpng
license) or zlib 1.3.1 (deflate.c, inflate.c, zlib license), wrapped in a function
`region(...)`; the first-line comment gives the file, line, function and
expression. The license texts are in `tests/c_samples_real2/LICENSES.md` (libpng)
and below (zlib).

## zlib

```
  Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
```
