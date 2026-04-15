<!-- SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only -->

# wardtest — Claude Code Project Instructions

## Architecture

- `src/wardtest.c` — main entry point, argument parsing, iteration loop
- `src/actions.c` — file operations (create, read, write, delete, unlink)
- `src/state.c` — filesystem state machine (empty → normal → full)
- `src/verify.c` — pattern generation and corruption detection
- `src/meta.c` — metadata record management
- `src/history.c` — history logging
- `src/machine.c` — machine ID generation for multi-client tracking
- `src/lock.c` — byte-level locking stress

## License

- All code: BSD-2-Clause OR GPL-2.0-only
- SPDX headers required on all files
- Co-Authored-By lines are permitted in this repo

## Git conventions

- Always sign off: `git commit -s`
- One concern per commit
- Run tests before committing

## Design

- Clean-room implementation — no code from prior art
- Works against any POSIX filesystem (local, NFS, FUSE)
- No NFS protocol dependency — uses POSIX API only
- Minimal dependencies: C11, pthreads, POSIX file I/O
