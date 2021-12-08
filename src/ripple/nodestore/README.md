# Database Documentation
* [NodeStore](#nodestore)
* [Benchmarks](#benchmarks)
* [Downloaded Shard Validation](#downloaded-shard-validation)
* [Shard Storage Paths](#shard-storage-paths)

# NodeStore

## Introduction

A `NodeObject` is a simple object that the Ledger uses to store entries. It is
comprised of a type, a hash and a blob. It can be uniquely
identified by the hash, which is a 256 bit hash of the blob. The blob is a
variable length block of serialized data. The type identifies what the blob
contains. The fields are as follows:

* `mType`

 An enumeration that determines what the blob holds. There are four
 different types of objects stored.

 * **ledger**

   A ledger header.

 * **transaction**

   A signed transaction.

 * **account node**

   A node in a ledger's account state tree.

 * **transaction node**

   A node in a ledger's transaction tree.

* `mHash`

 A 256-bit hash of the blob.

* `mData`

 A blob containing the payload. Stored in the following format.

|Byte   |                     |                          |
|:------|:--------------------|:-------------------------|
|0...7  |unused               |                          |
|8      |type                 |NodeObjectType enumeration|
|9...end|data                 |body of the object data   |
---
The `NodeStore` provides an interface that stores, in a persistent database, a
collection of NodeObjects that rippled uses as its primary representation of
ledger entries. All ledger entries are stored as NodeObjects and as such, need
to be persisted between launches. If a NodeObject is accessed and is not in
memory, it will be retrieved from the database.

## Backend

The `NodeStore` implementation provides the `Backend` abstract interface,
which lets different key/value databases to be chosen at run-time. This allows
experimentation with different engines. Improvements in the performance of the
NodeStore are a constant area of research. The database can be specified in
the configuration file [node_db] section as follows.

One or more lines of key / value pairs

Example:
```
type=RocksDB
path=rocksdb
compression=1
```
Choices for 'type' (not case-sensitive)

* **HyperLevelDB**

 An improved version of LevelDB (preferred).

* **LevelDB**

 Google's LevelDB database (deprecated).

* **none**

 Use no backend.

* **RocksDB**

 Facebook's RocksDB database, builds on LevelDB.

* **SQLite**

 Use SQLite.

'path' speficies where the backend will store its data files.

Choices for 'compression'

* **0** off

* **1** on (default)


# Benchmarks

The `NodeStore.Timing` test is used to execute a set of read/write workloads to
compare current available nodestore backends. It can be executed with:

```
$rippled --unittest=NodeStoreTiming
```

It is also possible to use alternate DB config params by passing config strings
as `--unittest-arg`.

## Addendum

The discussion below refers to a `RocksDBQuick` backend that has since been
removed from the code as it was not working and not maintained. That backend
primarily used one of the several rocks `Optimize*` methods to setup the
majority of the DB options/params, whereas the primary RocksDB backend exposes
many of the available config options directly. The code for RocksDBQuick can be
found in versions of this repo 1.2 and earlier if you need to refer back to it.
The conclusions below date from about 2014 and may need revisiting based on
newer versions of RocksDB (TBD).

## Discussion

RocksDBQuickFactory is intended to provide a testbed for comparing potential
rocksdb performance with the existing recommended configuration in rippled.cfg.
Through various executions and profiling some conclusions are presented below.

* If the write ahead log is enabled, insert speed soon clogs up under load. The
BatchWriter class intends to stop this from blocking the main threads by queuing
up writes and running them in a separate thread. However, rocksdb already has
separate threads dedicated to flushing the memtable to disk and the memtable is
itself an in-memory queue. The result is two queues with a guarantee of
durability in between. However if the memtable was used as the sole queue and
the rocksdb::Flush() call was manually triggered at opportune moments, possibly
just after ledger close, then that would provide similar, but more predictable
guarantees. It would also remove an unneeded thread and unnecessary memory
usage. An alternative point of view is that because there will always be many
other rippled instances running there is no need for such guarantees. The nodes
will always be available from another peer.

* Lookup in a block was previously using binary search. With rippled's use case
it is highly unlikely that two adjacent key/values will ever be requested one
after the other. Therefore hash indexing of blocks makes much more sense.
Rocksdb has a number of options for hash indexing both memtables and blocks and
these need more testing to find the best choice.

* The current Database implementation has two forms of caching, so the LRU cache
of blocks at Factory level does not make any sense. However, if the hash
indexing and potentially the new [bloom
filter](http://rocksdb.org/blog/1427/new-bloom-filter-format/) can provide
faster lookup for non-existent keys, then potentially the caching could exist at
Factory level.

* Multiple runs of the benchmarks can yield surprisingly different results. This
can perhaps be attributed to the asynchronous nature of rocksdb's compaction
process. The benchmarks are artifical and create highly unlikely write load to
create the dataset to measure different read access patterns. Therefore multiple
runs of the benchmarks are required to get a feel for the effectiveness of the
changes. This contrasts sharply with the keyvadb benchmarking were highly
repeatable timings were discovered. Also realistically sized datasets are
required to get a correct insight. The number of 2,000,000 key/values (actually
4,000,000 after the two insert benchmarks complete) is too low to get a full
picture.

* An interesting side effect of running the benchmarks in a profiler was that a
clear pattern of what RocksDB does under the hood was observable. This led to
the decision to trial hash indexing and also the discovery of the native CRC32
instruction not being used.

* Important point to note that is if this factory is tested with an existing set
of sst files none of the old sst files will benefit from indexing changes until
they are compacted at a future point in time.

# Downloaded Shard Validation

## Overview

In order to validate shards that have been downloaded from file servers (as
opposed to shards acquired from peers), the application must confirm the
validity of the downloaded shard's last ledger. So before initiating the
download, we first confirm that we are able to retrieve the shard's last ledger
hash. The following sections describe this confirmation process in greater
detail.

## Hash Verification

### Flag Ledger

Since the number of ledgers contained in each shard is always a multiple of 256,
a shard's last ledger is always a flag ledger. Conveniently, the skip list
stored within a ledger will provide us with a series of flag ledger hashes,
enabling the software to corroborate a shard's last ledger hash. We access the
skip list by calling `LedgerMaster::walkHashBySeq` and providing the sequence of
a shard's last ledger:

```C++
std::optional<uint256> expectedHash;
expectedHash =
    app_.getLedgerMaster().walkHashBySeq(lastLedgerSeq(shardIndex));
```

When a user requests a shard download, the `ShardArchiveHandler` will first use
this function to retrieve the hash of the shard's last ledger. If the function
returns a hash, downloading the shard can proceed. Once the download completes,
the server can reliably retrieve this last ledger hash to complete validation of
the shard.

### Caveats

#### Later Ledger

The `walkHashBySeq` function will provide the hash of a flag ledger only if the
application has stored a later ledger. When verifying the last ledger hash of a
pending shard download, if there is no later ledger stored, the download will be
deferred until a later ledger has been stored.

We use the presence (or absence) of a validated ledger with a sequence number
later than the sequence of the shard's last ledger as a heuristic for
determining whether or not we should have the shard's last ledger hash. A later
ledger must be present in order to reliably retrieve the hash of the shard's
last ledger. The hash will only be retrieved when a later ledger is present.
Otherwise verification of the shard will be deferred.

### Retries

#### Retry Limit

If the server must defer hash verification, the software will initiate a timer
that upon expiration, will re-attempt verifying the last ledger hash. We place
an upper limit on the number of attempts the server makes to achieve this
verification. When the maximum number of attempts has been reached, the download
request will fail, and the `ShardArchiveHandler` will proceed with any remaining
downloads. An attempt counts toward the limit only when we are able to get a
later validated ledger (implying a current view of the network), but are unable
to retrieve the last ledger hash. Retries that occur because no validated ledger
was available are not counted.

# Shard Storage Paths

## Overview

The shard database stores validated ledgers in logical groups called shards. As
of June 2020, a shard stores 16384 ledgers by default. In order to allow users
to store shards on multiple devices, the shard database can be configured with
several file system paths. Each path provided should refer to a directory on a
distinct filesystem, and no two paths should ever correspond to the same
filesystem. Violating this restriction will cause the server to inaccurately
estimate the amount of space available for storing shards. In the absence of a
suitable platform agnostic solution, this requirement is enforced only on
Linux. However, on other platforms we employ a heuristic that issues a warning
if we suspect that this restriction is violated.

## Configuration

The `shard_db` and `historical_shard_paths` sections of the server's
configuration file will be used to determine where the server stores shards.
Minimally, the `shard_db` section must contain a single `path` key.
If this is the only storage path provided, all shards will be stored at this
location. If the configuration also lists one or more lines in the
`historical_shard_paths` section, all older shards will be stored at these
locations, and the `path` will be used only to store the current
and previous shards. The goal is to allow users to provide an efficient SSD for
storing recent shards, as these will be accessed more frequently, while using
large mechanical drives for storing older shards that will be accessed less
frequently.

Below is a sample configuration snippet that provides a path for main storage
and several paths for historical storage:

```dosini
# This is the persistent datastore for shards. It is important for the health
# of the network that server operators shard as much as practical.
# NuDB requires SSD storage. Helpful information can be found on
# https://xrpl.org/history-sharding.html
[shard_db]
type=NuDB

# A single path for storing
# the current and previous
# shards:
# -------------------------
path=/var/lib/rippled/db/shards/nudb

# Path where shards are stored
# while being downloaded:
# ----------------------------
download_path=/var/lib/rippled/db/shards/

# The number of historical shards to store.
# The default value is 0, which means that
# the server won't store any historical
# shards - only the current and previous
# shards will be stored.
# ------------------------------------
max_historical_shards=100

# List of paths for storing older shards.
[historical_shard_paths]
/mnt/disk1
/mnt/disk2
/mnt/disk3

```
## Shard Migration

When a new shard (*current shard*) is confirmed by the network, the recent
shards will shift. The *previous shard* will become a *historical shard*, the
*current shard* will become the *previous shard*, and the new shard will become
the *current shard*. These are just logical labels, and the shards themselves
don't change to reflect being current, previous, or historical. However, if the
server's configuration specifies one or more paths for historical storage,
during this shift the formerly *previous shard* will be migrated to one of the
historical paths. If multiple paths are provided, the server dynamically
chooses one with sufficient space for storing the shard.

**Note:** As of June 2020, the shard database does not store the partial shard
currently being built by live network transactions, but this is planned to
change. When this feature is implemented, the *current shard* will refer to this
partial shard, and the *previous shard* will refer to the most recently
validated shard.

### Selecting a Historical Storage Path

When storing historical shards, if multiple historical paths are provided, the
path to use for each shard will be selected in a random fashion. By using all
available storage devices, we create a uniform distribution of disk utilization
for disks of equivalent size, (provided that the disks are used only to store
shards). In theory, selecting devices in this manner will also increase our
chances for concurrent access to stored shards, however as of June 2020
concurrent shard access is not implemented. Lastly, a storage path is included
in the random distribution only if it has enough storage capacity to hold the
next shard.

## Shard Acquisition

When the server is acquiring shard history, these acquired shards will be stored
at a path designated for historical storage (`historical_storage_path`). If no
such path is provided, acquired shards will be stored at the
`path`.

## Storage capacity

### Filesystem Capacity

When the shard database updates its record of disk utilization, it trusts that
the provided historical paths refer to distinct devices, or at least distinct
filesystems. If this requirement is violated, the database will operate with an
inaccurate view of how many shards it can store. Violation of this requirement
won't necessarily impede database operations, but the database will fail to
identify scenarios wherein storing the maximum number of historical shards (as
per the 'historical_shard_count' parameter in the configuration file) would
exceed the amount of storage space available.

### Shard Migration

During a "recent shard shift", if the server has already reached the configured
limit of stored historical shards, instead of moving the formerly *previous
shard* to a historical drive (or keeping it at the 'path') the
shard will be dropped and removed from the filesystem.

### Shard Acquisition

Once the configured limit of stored historical shards has been reached, shard
acquisition halts, and no additional shards will be acquired.
