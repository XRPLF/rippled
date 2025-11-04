#ifndef XRPL_UNITY_ROCKSDB_H_INCLUDED
#define XRPL_UNITY_ROCKSDB_H_INCLUDED

#if XRPL_ROCKSDB_AVAILABLE
// #include <rocksdb2/port/port_posix.h>
#include <rocksdb/cache.h>
#include <rocksdb/compaction_filter.h>
#include <rocksdb/comparator.h>
#include <rocksdb/convenience.h>
#include <rocksdb/db.h>
#include <rocksdb/env.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/flush_block_policy.h>
#include <rocksdb/iterator.h>
#include <rocksdb/memtablerep.h>
#include <rocksdb/merge_operator.h>
#include <rocksdb/options.h>
#include <rocksdb/perf_context.h>
#include <rocksdb/slice.h>
#include <rocksdb/slice_transform.h>
#include <rocksdb/statistics.h>
#include <rocksdb/status.h>
#include <rocksdb/table.h>
#include <rocksdb/table_properties.h>
#include <rocksdb/transaction_log.h>
#include <rocksdb/types.h>
#include <rocksdb/universal_compaction.h>
#include <rocksdb/write_batch.h>

#endif

#endif
