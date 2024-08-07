# Database Documentation
* [NodeStore](#nodestore)
* [Benchmarks](#benchmarks)

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
