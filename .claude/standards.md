<!-- SPDX-License-Identifier: Apache-2.0 -->

# wardtest Coding Standards

## Style

- Indentation: **tabs**, width 8
- Line length: **80 columns**
- Pointer alignment: right (`int *ptr`, not `int* ptr`)
- Function opening brace: new line

## SPDX Headers

Every source file must begin with:
```c
/* SPDX-FileCopyrightText: 2026 Tom Haynes <loghyr@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
```

Shell/Makefile/YAML use `#` comment equivalents.
Markdown uses `<!-- -->`.

## Build

Build in a subdirectory — never in the source tree:
```bash
mkdir -p m4 && autoreconf -fi
mkdir build && cd build
../configure
make -j$(nproc)
```

## Error Handling

- Use specific return codes (0 success, negative errno on failure)
- Never silently swallow errors
- LOG for actionable errors, TRACE/printf for diagnostics
- **Every system call return value must be checked.**  wardtest detects
  filesystem corruption — an unchecked system call can introduce
  corruption programmatically, defeating the purpose of the tool.
  This applies to: `write()`, `close()`, `fsync()`, `rename()`,
  `unlink()`, `clock_gettime()`, `gethostname()`, `snprintf()`
  (check for truncation), and any other libc/POSIX call that can fail.
- In best-effort code paths (history logging, client registration),
  check the return and log/skip on failure — do not `(void)` cast.

## Unused Parameters

Use `__attribute__((unused))`, never `(void)param;`.

## Git

- Always sign off: `git commit -s`
- Co-Authored-By lines are permitted
- One concern per commit
- Run tests before committing

## Dependencies

Minimal:
- C11, pthreads
- POSIX file I/O
- CRC32 (zlib or inline)
- Optional: xxhash

No NFS protocol dependency.  No kernel dependency.
