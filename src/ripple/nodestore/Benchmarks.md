#Benchmarks

```
$rippled --unittest=NodeStoreTiming --unittest-arg="type=rocksdbquick,style=level,num_objects=2000000"

ripple.bench.NodeStoreTiming repeatableObject
  Batch Insert    Fetch 50/50  Fetch Missing   Fetch Random        Inserts  Ordered Fetch
         59.53          12.67           6.04          11.33          25.55          52.15 type=rocksdbquick,style=level,num_objects=2000000

$rippled --unittest=NodeStoreTiming --unittest-arg="type=rocksdbquick,style=level,num_objects=2000000"

ripple.bench.NodeStoreTiming repeatableObject
  Batch Insert    Fetch 50/50  Fetch Missing   Fetch Random        Inserts  Ordered Fetch
         44.29          27.45           5.95          20.47          23.58          53.60 type=rocksdbquick,style=level,num_objects=2000000
```

```
$rippled --unittest=NodeStoreTiming --unittest-arg="type=rocksdb,num_objects=2000000,open_files=2000,filter_bits=12,cache_mb=256,file_size_mb=8,file_size_mult=2"

ripple.bench.NodeStoreTiming repeatableObject
  Batch Insert    Fetch 50/50  Fetch Missing   Fetch Random        Inserts  Ordered Fetch
        377.61          30.62          10.05          17.41         201.73          64.46 type=rocksdb,num_objects=2000000,open_files=2000,filter_bits=12,cache_mb=256,file_size_mb=8,file_size_mult=2

$rippled --unittest=NodeStoreTiming --unittest-arg="type=rocksdb,num_objects=2000000,open_files=2000,filter_bits=12,cache_mb=256,file_size_mb=8,file_size_mult=2"

ripple.bench.NodeStoreTiming repeatableObject
  Batch Insert    Fetch 50/50  Fetch Missing   Fetch Random        Inserts  Ordered Fetch
        405.83          29.48          11.29          25.81         209.05          55.75 type=rocksdb,num_objects=2000000,open_files=2000,filter_bits=12,cache_mb=256,file_size_mb=8,file_size_mult=2
```

##Discussion

RocksDBQuickFactory is intended to provide a testbed for comparing potential rocksdb performance with the existing recommended configuration in rippled.cfg. Through various executions and profiling some conclusions are presented below.

* If the write ahead log is enabled, insert speed soon clogs up under load. The BatchWriter class intends to stop this from blocking the main threads by queuing up writes and running them in a separate thread. However, rocksdb already has separate threads dedicated to flushing the memtable to disk and the memtable is itself an in-memory queue. The result is two queues with a guarantee of durability in between. However if the memtable was used as the sole queue and the rocksdb::Flush() call was manually triggered at opportune moments, possibly just after ledger close, then that would provide similar, but more predictable guarantees. It would also remove an unneeded thread and unnecessary memory usage. An alternative point of view is that because there will always be many other rippled instances running there is no need for such guarantees. The nodes will always be available from another peer.

* Lookup in a block was previously using binary search. With rippled's use case it is highly unlikely that two adjacent key/values will ever be requested one after the other. Therefore hash indexing of blocks makes much more sense. Rocksdb has a number of options for hash indexing both memtables and blocks and these need more testing to find the best choice.

* The current Database implementation has two forms of caching, so the LRU cache of blocks at Factory level does not make any sense. However, if the hash indexing and potentially the new [bloom filter](http://rocksdb.org/blog/1427/new-bloom-filter-format/) can provide faster lookup for non-existent keys, then potentially the caching could exist at Factory level.

* Multiple runs of the benchmarks can yield surprisingly different results. This can perhaps be attributed to the asynchronous nature of rocksdb's compaction process. The benchmarks are artifical and create highly unlikely write load to create the dataset to measure different read access patterns. Therefore multiple runs of the benchmarks are required to get a feel for the effectiveness of the changes. This contrasts sharply with the keyvadb benchmarking were highly repeatable timings were discovered. Also realistically sized datasets are required to get a correct insight. The number of 2,000,000 key/values (actually 4,000,000 after the two insert benchmarks complete) is too low to get a full picture.

* An interesting side effect of running the benchmarks in a profiler was that a clear pattern of what RocksDB does under the hood was observable. This led to the decision to trial hash indexing and also the discovery of the native CRC32 instruction not being used.

* Important point to note that is if this factory is tested with an existing set of sst files none of the old sst files will benefit from indexing changes until they are compacted at a future point in time.
