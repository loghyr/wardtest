<!-- SPDX-License-Identifier: Apache-2.0 -->

# wardtest Deployment Scenarios

wardtest uses POSIX file APIs — it tests whatever the mount
underneath provides.  The deployment topology determines what
you're testing.

## Scenario A: Test the Server (multiple clients)

The primary use case.  Multiple clients each with their own NFS
mount, own TCP connection, own NFS session.

```
┌──────────┐     NFSv4.2     ┌──────────────┐
│ client-1 │────────────────►│              │
│ wardtest  │                 │  NFS Server  │
└──────────┘                 │  (reffs,     │
                             │   knfsd,     │
┌──────────┐     NFSv4.2     │   NetApp,    │
│ client-2 │────────────────►│   etc.)      │
│ wardtest  │                 │              │
└──────────┘                 │  /export     │
                             │   ├── data/  │
┌──────────┐     NFSv4.2     │   ├── meta/  │
│ client-3 │────────────────►│   └── hist/  │
│ wardtest  │                 │              │
│ --verify  │                 └──────────────┘
└──────────┘
```

**What it tests:**
- Server-side data integrity under concurrent access
- Write ordering and persistence (do writes from client-1 appear
  correctly when client-3 reads?)
- Lock manager correctness (if byte-level locking is enabled)
- Server crash recovery (kill the server, restart, verify all data)

**How to run:**

On each client:
```bash
# Mount with noac to disable client caching — forces every
# read/write to go to the server.  This is a stress test,
# not a performance test.
mount -o vers=4.2,noac server:/export /mnt/nfs

# Client 1 & 2: write + verify
wardtest --data /mnt/nfs/data --meta /mnt/nfs/meta \
    --history /mnt/nfs/hist --iterations 10000 --clients 2

# Client 3: verify only (reads other clients' stripes)
wardtest --data /mnt/nfs/data --meta /mnt/nfs/meta \
    --history /mnt/nfs/hist --verify-only
```

**Interpreting results:**

If client-3 (verify-only) detects corruption:
- The server lost or mangled data between the write (client-1/2)
  and the read (client-3)
- Check: is the CRC in the chunk header correct?
  - CRC correct but data doesn't match seed → server stored the
    wrong data (write went to wrong offset, stale cache, etc.)
  - CRC wrong → the write itself was corrupted in transit or the
    server mangled the block on disk

## Scenario B: Test the Client (single client, multiple threads)

Single NFS mount, multiple writer/verifier threads within one
wardtest process.

```
┌─────────────────────────┐        ┌────────────┐
│ client-1                │  NFS   │            │
│  ├── thread 1 (writer)  │───────►│  Server    │
│  ├── thread 2 (writer)  │        │            │
│  ├── thread 3 (writer)  │        └────────────┘
│  └── thread 4 (verify)  │
│ wardtest --clients 4     │
└─────────────────────────┘
```

**What it tests:**
- Client-side write caching and flush behavior
- Delegation handling under concurrent access
- close-to-open consistency within a single mount
- Client-side lock management
- Page cache coherency

**How to run:**
```bash
mount -o vers=4.2 server:/export /mnt/nfs

# All threads share one mount — tests client-side concurrency
wardtest --data /mnt/nfs/data --meta /mnt/nfs/meta \
    --history /mnt/nfs/hist --clients 4 --iterations 50000
```

**Interpreting results:**

Corruption in this scenario is usually the client's fault:
- Stale page cache (write by thread 1 not visible to thread 4)
- Delegation conflict not handled (thread 1 has write delegation,
  thread 2 writes same file without recall)
- close-to-open not enforced (thread 4 reads stale data after
  thread 1 closed and reopened)

## Scenario C: Test the Whole Stack (mixed)

Combines A and B.  Writers on multiple clients with multiple
threads each, dedicated verifiers on separate clients.

```
┌──────────────────┐            ┌────────────┐
│ client-1         │    NFS     │            │
│  ├── 4 writers   │───────────►│  Server    │
│  └── 1 verifier  │            │            │
└──────────────────┘            │  /export   │
                                │            │
┌──────────────────┐    NFS     │            │
│ client-2         │───────────►│            │
│  ├── 4 writers   │            │            │
│  └── 1 verifier  │            └────────────┘
└──────────────────┘
                     ┌──────────────────┐
                     │ client-3         │
                     │  verify-only     │
                     │  (cross-client)  │
                     └──────────────────┘
```

**What it tests:**
- Everything from A and B simultaneously
- Cross-client consistency (client-1 writes, client-2 verifies)
- Server behavior under mixed read/write load from multiple sources
- Full-stack data integrity: client caching + wire protocol +
  server storage + server caching

**How to run:**
```bash
# Client 1 & 2: heavy writers + local verify
wardtest --data /mnt/nfs/data --meta /mnt/nfs/meta \
    --history /mnt/nfs/hist --clients 5 --iterations 100000

# Client 3: verify-only (catches cross-client corruption)
wardtest --data /mnt/nfs/data --meta /mnt/nfs/meta \
    --history /mnt/nfs/hist --verify-only
```

## Mount Options

**For stress testing (maximum server exposure):**
```
mount -o vers=4.2,noac,sec=sys server:/export /mnt/nfs
```
- `noac` — disables attribute caching, forces GETATTR on every stat
- Every read goes to the server (no client page cache reuse)
- Slowest but most thorough

**For realistic testing:**
```
mount -o vers=4.2,sec=sys server:/export /mnt/nfs
```
- Default caching — tests the real-world code path
- Faster, but some corruptions may be masked by client cache

**For Kerberos:**
```
mount -o vers=4.2,sec=krb5,noac server:/export /mnt/nfs
```
- Tests authenticated I/O path
- Exercises GSS context lifecycle under load

**For NFSv3 (lock manager testing):**
```
mount -o vers=3,noac,tcp server:/export /mnt/nfs
```
- Tests NLM (network lock manager) instead of NFSv4 locks
- `tcp` — ensures reliable transport for lock state

## Server Crash Recovery Test

The most valuable test for a new NFS server:

1. Start wardtest on 2+ clients with `--iterations 0` (infinite)
2. Let it run for 10 minutes (build up data)
3. Kill the server (`kill -9`)
4. Restart the server
5. Verify all data:
   ```bash
   wardtest --data /mnt/nfs/data --meta /mnt/nfs/meta \
       --history /mnt/nfs/hist --verify-only
   ```
6. If verify-only finds corruption, the server lost data during
   the crash or failed to recover correctly

This exercises:
- Write-ahead log / journal recovery
- Delegation recall after restart
- Grace period and state reclaim
- On-disk format consistency after unclean shutdown

## Interpreting the Chunk Header

When corruption is detected, wardtest reports the chunk header:

```
CORRUPTION stripe=4567 shard=2
  CRC expected: 0xaabbccdd  actual: 0x11223344
  machine_id: 0xdeadbeef (client-1, pid 12345)
  timestamp: 2026-04-03T14:23:45Z
  shard_size: 4096  k=4 m=1  seed=0x12345678
```

- **CRC mismatch**: the shard data on disk doesn't match the CRC
  in the header.  Either the write was corrupted, or the data was
  modified after write without updating the CRC (stale data from
  a different stripe).

- **EC decode failure**: the k+m shards don't produce a consistent
  decode.  One or more shards were corrupted or swapped.

- **Seed mismatch**: EC decode succeeds but the decoded data
  doesn't match what the seed should generate.  The wrong data
  was written (e.g., write went to wrong file/offset).

## Stopping and Restarting

**Clean stop**: Ctrl-C or `kill <pid>` — wardtest finishes the
current operation and exits cleanly.

**Corruption stop**: wardtest creates `.wardtest_stop` in the meta
directory.  All clients see it and stop.  To restart after
investigation: `rm /mnt/nfs/meta/.wardtest_stop`

**Fresh start**: remove all three directories and start over:
```bash
rm -rf /mnt/nfs/data /mnt/nfs/meta /mnt/nfs/hist
```
