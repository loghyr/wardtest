<!-- SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only -->

# wardtest — Claude Code Project Instructions

## Architecture

- `src/wardtest.c` — main entry point, argument parsing, iteration loop
- `src/actions.c` — stripe operations (create, verify, write/modify, delete)
- `src/state.c` — filesystem state machine (empty → normal → full)
- `src/codec.c` — codec dispatch; routes encode/verify to XOR or Reed-Solomon
- `src/xor.c` — XOR parity codec (k data shards + 1 parity)
- `src/rs.c` — Reed-Solomon GF(2^8) codec (k data + m parity shards)
- `src/chunk.c` — shard I/O with CRC-protected headers
- `src/crc32.c` — CRC32 (ISO 3309 / ITU-T V.42)
- `src/control.c` — control file; shared encoding parameters across clients
- `src/meta.c` — per-stripe metadata I/O (one file per stripe)
- `src/rng.c` — deterministic RNG (seeds stripe data)
- `src/history.c` — per-client append-only history log
- `src/machine.c` — machine ID generation for multi-client tracking
- `src/stop.c` — stop mechanism (eventfd for threads, sentinel file across clients)
- `src/lock.c` — POSIX byte-range lock stress (hotspot RMW under fcntl locks)

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
