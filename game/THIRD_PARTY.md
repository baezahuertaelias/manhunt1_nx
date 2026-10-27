# Third-party code in game/

| Path | Origin | License |
|---|---|---|
| `src/recomp_manual.c` (all but the "Manhunt" section) | sp00nznet/xboxrecomp `f0f8a461496a4f23d9b0aeaa284b8d70ba7ab82b`, `templates/new-game/src/recomp_manual.c` | MIT, Copyright (c) 2026 sp00nz |
| `src/main.c` (boot order, fault report) | adapted from the same template's `main.c` | MIT, Copyright (c) 2026 sp00nz |
| `patches/xboxrecomp/*.patch` | changes to xboxrecomp at the same commit | MIT (as the patched files) |

xboxrecomp itself is not vendored: `infra/recompiler/Dockerfile` pins and fetches it. Only its MIT parts (lifter, runtime templates, platform and kernel layers) are built into the game. The LGPL-2.1 xemu-derived files (`src/apu`, `src/nv2a/nv2a_regs.h`, `nv2a_core.c`, `nv2a_state.h`) are compiled in by the toolkit's CMake as `xbox_apu`/`xbox_nv2a`. **UNVERIFIED:** whether the final link pulls in any of their objects. Check this before distributing any binary. No binary is distributed today.

The MIT license text:

```
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
