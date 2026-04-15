<!-- SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only -->

# wardtest — filesystem and NFS stress test with corruption detection

wardtest creates randomized file structures on a filesystem and
performs concurrent create/read/write/delete operations while
verifying data integrity.  Designed to stress-test NFS servers,
lock managers, and distributed filesystems.

The name comes from "ward" — the part of a lock mechanism that
prevents the wrong key from turning.  wardtest verifies that data
written through NFS comes back unchanged.

## Features

- Weighted state machine adapts operations based on filesystem fullness
- Pattern-based corruption detection (write deterministic data, verify on read)
- Three-directory model (data, meta, history) for cross-volume stress
- Multi-client support with machine ID tracking
- Byte-level locking stress for lock manager testing
- Works against any NFS server (Linux knfsd, reffs, NetApp, etc.)

## Quick Start

```bash
mkdir -p m4 && autoreconf -fi
mkdir -p build && cd build
../configure
make -j$(nproc)

# Local filesystem test
./wardtest --data /tmp/wt_data --meta /tmp/wt_meta --history /tmp/wt_hist

# NFS stress test (mount with noac for strict consistency)
mount -o vers=4.2,noac server:/export /mnt/nfs
./wardtest --data /mnt/nfs/data --meta /mnt/nfs/meta --history /mnt/nfs/hist \
    --iterations 10000 --clients 4
```

## License

BSD-2-Clause OR GPL-2.0-only. See [LICENSE](LICENSE) for the full text.
