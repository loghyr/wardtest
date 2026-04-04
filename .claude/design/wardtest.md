<!-- SPDX-License-Identifier: Apache-2.0 -->

# wardtest Design

## Goal

Filesystem and NFS stress test that detects data corruption using
erasure coding.  When corruption is found, all writers stop
immediately to preserve the crime scene for analysis.

## Core Concept

Instead of writing simple byte patterns, wardtest EC-encodes source
data into k data shards + m parity shards, writes each shard as a
file with a CRC-protected chunk header.  Verification reads the
shards, checks CRCs, decodes, and compares against the regenerated
source.  Any mismatch triggers a global stop.

This exercises the same data integrity path as real pNFS erasure
coding -- CRC per block, EC decode for reconstruction, deterministic
source generation for verification.

## Encoding: XOR Parity (fast) + optional RS

### Default: XOR parity (k + 1)

For maximum speed, the default encoder is simple XOR parity:
- k data shards + 1 parity shard
- Parity = XOR of all data shards
- Verification: XOR all k+1 shards -> must be zero
- Detects any single-shard corruption
- O(k × shard_size) -- memory bandwidth limited, no GF math

```c
/* Encode: parity = data[0] ^ data[1] ^ ... ^ data[k-1] */
void xor_encode(const uint8_t **data, uint8_t *parity,
                size_t shard_size, int k)
{
    memcpy(parity, data[0], shard_size);
    for (int i = 1; i < k; i++)
        for (size_t j = 0; j < shard_size; j++)
            parity[j] ^= data[i][j];
}

/* Verify: XOR all shards including parity -> should be all zeros */
bool xor_verify(const uint8_t **shards, size_t shard_size, int n)
{
    uint8_t check[shard_size];
    memcpy(check, shards[0], shard_size);
    for (int i = 1; i < n; i++)
        for (size_t j = 0; j < shard_size; j++)
            check[j] ^= shards[i][j];
    return memcmp(check, zeros, shard_size) == 0;
}
```

### Optional: RS(k, m) for deeper testing

Command-line flag `--codec rs --k 4 --m 2` uses Reed-Solomon for
multi-shard corruption detection.  Uses the same textbook GF(2^8)
log/antilog table approach as reffs (pre-2000 sources, patent-safe).
Slower but catches multi-shard corruption.

## Chunk Header

Each shard file has a header:

```c
#define WARDTEST_CHUNK_MAGIC 0x57415244  /* "WARD" */
#define WARDTEST_CHUNK_VERSION 1

struct wt_chunk_header {
    uint32_t wch_magic;
    uint32_t wch_version;
    uint32_t wch_crc32;       /* CRC of shard data (not header) */
    uint32_t wch_shard_size;
    uint64_t wch_stripe_id;   /* unique ID for this stripe */
    uint32_t wch_shard_index; /* 0..k-1 = data, k..k+m-1 = parity */
    uint32_t wch_k;           /* data shard count */
    uint32_t wch_m;           /* parity shard count */
    uint32_t wch_seed;        /* RNG seed for source regeneration */
    uint64_t wch_machine_id;  /* writer's machine ID */
    uint64_t wch_timestamp;   /* CLOCK_REALTIME at write time */
};
```

Size: 64 bytes.  Followed by `wch_shard_size` bytes of shard data.

## Directory Model

Three directories (can be same or different mounts):

```
--data /mnt/nfs1/wt_data     Shard files: stripe_NNNN_shard_M
--meta /mnt/nfs2/wt_meta     Stripe metadata: stripe_NNNN.meta
--history /mnt/nfs3/wt_hist  Action log: history.log
```

### Stripe metadata file

```c
struct wt_stripe_meta {
    uint64_t wsm_stripe_id;
    uint32_t wsm_seed;
    uint32_t wsm_k;
    uint32_t wsm_m;
    uint32_t wsm_shard_size;
    uint32_t wsm_source_size; /* original data size before EC */
    uint64_t wsm_machine_id;
    uint64_t wsm_created_ns;
    uint32_t wsm_state;       /* ACTIVE, VERIFIED, CORRUPTED */
    uint32_t wsm_verify_count;
};
```

## State Machine

Same concept as elham -- adapt operation weights based on filesystem
fullness:

| State | Creates | Reads | Writes | Deletes | Verify |
|-------|---------|-------|--------|---------|--------|
| EMPTY | 80% | 5% | 5% | 0% | 10% |
| NORMAL | 20% | 25% | 25% | 10% | 20% |
| FULL | 0% | 20% | 20% | 40% | 20% |

State transitions based on `statvfs()` free space percentage.

## Operations

### Create

1. Generate random source data from seed
2. EC-encode -> k data + m parity shards
3. CRC32 each shard
4. Write each shard to `.tmp` file, fsync, rename to final name
   (atomic -- a crash mid-create leaves only `.tmp` files)
5. Write stripe metadata (also atomic: write-temp/fsync/rename)
   Metadata written AFTER all shards committed -- verifier
   only sees complete stripes.
6. Append to history (per-client file, O_APPEND for atomicity)

On startup: scan for orphaned `.tmp` files and remove them.

### Read + Verify

1. Pick a random existing stripe (from meta directory scan)
2. Read all k+m shard files
3. Verify CRC32 on each shard
4. EC-verify (XOR check or RS decode)
5. Regenerate source from seed, EC-encode, compare
6. On success: bump `wsm_verify_count`
7. **On failure: STOP ALL WRITERS**

### Write (modify)

1. Pick a random existing stripe
2. Generate new source data with new seed
3. Re-encode, write new shards (atomic: write-temp/fsync/rename)
4. Update metadata with new seed (atomic)
5. Append to history

### Delete

1. Pick a random existing stripe
2. Unlink all shard files
3. Unlink metadata file
4. Append to history

## Stop Mechanism

Global stop on corruption detection:

```c
static volatile sig_atomic_t g_stop = 0;
static int g_stop_fd = -1;  /* eventfd for cross-thread notification */

void wardtest_stop(const char *reason, uint64_t stripe_id,
                   int shard_index)
{
    g_stop = 1;
    LOG("CORRUPTION DETECTED: %s stripe=%lu shard=%d",
        reason, stripe_id, shard_index);

    /* Wake all threads via eventfd */
    uint64_t val = 1;
    write(g_stop_fd, &val, sizeof(val));
}
```

Every writer checks `g_stop` before each operation:

```c
if (g_stop) {
    LOG("Writer %d stopping (corruption detected by another thread)");
    return;
}
```

On stop:
- All writers exit their loop
- Verifier dumps diagnostic info (which shard, expected vs actual CRC)
- All files are preserved (no cleanup)
- Exit code indicates corruption found

## Multi-Client Coordination

No central coordinator.  Clients discover each other and coordinate
via sentinel files on the shared filesystem -- which is exactly what
we're testing.

Machine ID: `XXH64(hostname, pid)` -- unique per process per host.

### Sentinel files

Two files in the meta directory:

**`.wardtest_stop`** -- existence means STOP.  Cheap to check:
one `access(path, F_OK)` syscall per operation cycle.

```
CORRUPTION stripe=4567 shard=2
machine=0xdeadbeef host=client-1 pid=12345
time=2026-04-03T14:23:45Z
expected_crc=0xaabbccdd actual_crc=0x11223344
```

**`.wardtest_clients`** -- append-only log of client lifecycle.
Each line ≤ 256 bytes (atomic via O_APPEND on Linux):

```
RUNNING  0xdeadbeef client-1 12345 2026-04-03T14:00:00Z
RUNNING  0xcafebabe client-2 67890 2026-04-03T14:00:05Z
DONE     0xdeadbeef client-1 12345 2026-04-03T14:30:00Z iterations=10000
DONE     0xcafebabe client-2 67890 2026-04-03T14:30:02Z iterations=10000
```

### Start protocol

1. Check `.wardtest_stop` -- if exists, refuse to start (crime scene)
2. Append `RUNNING` to `.wardtest_clients`
3. Scan for orphaned `.tmp` files, remove them
4. Begin operations

### Per-operation check

Before every create/write/delete:
```c
if (g_stop || access(stop_path, F_OK) == 0) {
    /* Another client (or thread) detected corruption */
    g_stop = 1;
    return;
}
```

### Stop protocol (corruption)

1. Set `g_stop = 1` + eventfd wake (stops local threads)
2. Create `.wardtest_stop` with diagnostic info
3. Other clients see it on next operation cycle, stop
4. All files preserved for analysis

### End protocol (clean)

1. Finish iterations
2. Append `DONE` to `.wardtest_clients`
3. Exit 0

### Restart after investigation

Operator clears the stop state:
```bash
rm /mnt/nfs/meta/.wardtest_stop
# Optionally: rm -rf all three directories for fresh start
```

### SIGTERM handling

SIGTERM -> graceful stop (finish current operation, append `DONE`).
Corruption stop -> immediate (preserve crime scene).

A **dedicated verifier** mode (`--verify-only`) reads and verifies
without writing -- run on a separate client to detect corruption
from the writers' perspective.

## Command Line

```
wardtest [options]

Required:
  --data PATH        Directory for shard files
  --meta PATH        Directory for stripe metadata
  --history PATH     Directory for action history

Optional:
  --iterations N     Number of operations (default: infinite)
  --clients N        Number of writer threads (default: 1)
  --shard-size N     Shard size in bytes (default: 4096)
  --k N              Data shards (default: 4)
  --m N              Parity shards (default: 1 for XOR, 2 for RS)
  --codec TYPE       xor (default) or rs
  --verify-only      Read and verify only, no writes
  --seed N           Base RNG seed (default: time-based)
  --stop-on-error    Stop all threads on first corruption (default: on)
  --report-interval  Print stats every N seconds (default: 10)
```

## Output

```
[00:05:00] Stats: created=1234 read=5678 write=2345 delete=456 verify=8901
           Verified: 8901/8901 (100%)  Corrupted: 0
           Filesystem: 45% used  State: NORMAL
[00:10:00] CORRUPTION DETECTED: CRC mismatch stripe=4567 shard=2
           Expected CRC: 0xaabbccdd  Actual: 0x11223344
           Machine ID: 0xdeadbeef (writer-host-1)
           Timestamp: 2026-04-03T14:23:45Z
           ALL WRITERS STOPPED -- files preserved for analysis
```

## Implementation Steps

### Step 1: Core infrastructure

- `src/wardtest.c` -- main, arg parsing, thread management
- `src/wardtest.h` -- all structs, constants, function declarations
- `src/machine.c` -- machine ID generation
- `src/rng.c` -- deterministic RNG (XXH64-based for speed)
- Build system (autotools)
- Unit test for RNG determinism

### Step 2: XOR encoder + chunk I/O

- `src/xor.c` -- XOR encode/verify
- `src/chunk.c` -- chunk header read/write with CRC32
- Unit tests: encode -> write -> read -> verify round-trip

### Step 3: Operations + state machine

- `src/actions.c` -- create, read+verify, write, delete
- `src/state.c` -- filesystem state tracking, weight adjustment
- `src/meta.c` -- stripe metadata read/write
- `src/history.c` -- action logging
- Integration test: run 1000 iterations, verify 0 corruption

### Step 4: Multi-threading + stop mechanism

- Thread pool with per-thread RNG state
- eventfd-based stop propagation
- Concurrent writer + verifier test

### Step 5: RS encoder (optional)

- `src/rs.c` -- GF(2^8) log/antilog RS encode/decode
- Same API as XOR -- drop-in replacement via `--codec rs`

### Step 6: Multi-client support

- Machine ID in chunk headers and metadata
- Verify-only mode
- Cross-client verification test

## Dependencies

- C11, pthreads
- POSIX file I/O (open, read, write, ftruncate, rename, unlink)
- `statvfs` for filesystem state detection
- CRC32 (zlib or inline implementation)
- Optional: xxhash for machine ID and fast RNG

No NFS protocol dependency.  No kernel dependency.  Works on any
POSIX filesystem.

## Testing

| Test | Method |
|------|--------|
| RNG determinism | Same seed -> same output |
| XOR encode/verify | Round-trip: encode -> verify -> pass |
| XOR corruption detect | Flip bit -> verify -> fail |
| Chunk header CRC | Write -> read -> verify CRC |
| Chunk header corrupt | Modify byte -> CRC mismatch |
| Create + verify stripe | Full pipeline: generate -> encode -> write -> read -> decode -> compare |
| State machine transitions | statvfs mock: empty -> normal -> full |
| Stop mechanism | Inject corruption -> all threads stop |
| Multi-thread safety | 4 writers + 1 verifier, 10000 iterations, 0 corruption |
