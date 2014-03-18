//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include "../../BeastConfig.h"

#include "ripple_rocksdb.h"

#if RIPPLE_ROCKSDB_AVAILABLE

#if BEAST_WIN32
# define ROCKSDB_PLATFORM_WINDOWS
#else
# define ROCKSDB_PLATFORM_POSIX
# if BEAST_MAC || BEAST_IOS
#  define OS_MACOSX
# elif BEAST_BSD
#  define OS_FREEBSD
# else
#  define OS_LINUX
# endif
#endif

#if BEAST_GCC
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wreorder"
# pragma GCC diagnostic ignored "-Wunused-variable"
# pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif

// Compile RocksDB without debugging unless specifically requested
#if !defined (NDEBUG) && !defined (RIPPLE_DEBUG_ROCKSDB)
#define NDEBUG
#endif

#include "rocksdb/db/builder.cc"
#include "rocksdb/db/db_filesnapshot.cc"
#include "rocksdb/db/dbformat.cc"
#include "rocksdb/db/db_impl.cc"
#include "rocksdb/db/db_impl_readonly.cc"
#include "rocksdb/db/db_iter.cc"
#include "rocksdb/db/db_stats_logger.cc"
#include "rocksdb/db/filename.cc"
#include "rocksdb/db/log_reader.cc"
#include "rocksdb/db/log_writer.cc"
#include "rocksdb/db/memtable.cc"
#include "rocksdb/db/memtablelist.cc"
#include "rocksdb/db/merge_helper.cc"
#include "rocksdb/db/merge_operator.cc"
#include "rocksdb/db/repair.cc"
#include "rocksdb/db/table_cache.cc"
#include "rocksdb/db/table_properties_collector.cc"
#include "rocksdb/db/transaction_log_impl.cc"
#include "rocksdb/db/version_edit.cc"
#include "rocksdb/db/version_set.cc"
#include "rocksdb/db/version_set_reduce_num_levels.cc"
#include "rocksdb/db/write_batch.cc"

#include "rocksdb/table/block_based_table_builder.cc"
#include "rocksdb/table/block_based_table_factory.cc"
#include "rocksdb/table/block_based_table_reader.cc"
#include "rocksdb/table/block_builder.cc"
#include "rocksdb/table/block.cc"
#include "rocksdb/table/filter_block.cc"
#include "rocksdb/table/flush_block_policy.cc"
#include "rocksdb/table/format.cc"
#include "rocksdb/table/iterator.cc"
#include "rocksdb/table/merger.cc"
#include "rocksdb/table/two_level_iterator.cc"

#include "rocksdb/util/arena_impl.cc"
#include "rocksdb/util/auto_roll_logger.cc"
#include "rocksdb/util/blob_store.cc"
#include "rocksdb/util/bloom.cc"
#include "rocksdb/util/cache.cc"
#include "rocksdb/util/coding.cc"
#include "rocksdb/util/comparator.cc"
#include "rocksdb/util/crc32c.cc"
#include "rocksdb/util/env.cc"
//#include "rocksdb/util/env_hdfs.cc"
#include "rocksdb/util/env_posix.cc"
#include "rocksdb/util/filter_policy.cc"
#include "rocksdb/util/hash.cc"
#include "rocksdb/util/hash_skiplist_rep.cc"
#include "rocksdb/util/histogram.cc"
#include "rocksdb/util/logging.cc"
#include "rocksdb/util/murmurhash.cc"
#include "rocksdb/util/options.cc"
#include "rocksdb/util/perf_context.cc"
#include "rocksdb/util/skiplistrep.cc"
#include "rocksdb/util/slice.cc"
#include "rocksdb/util/statistics.cc"
#include "rocksdb/util/status.cc"
#include "rocksdb/util/string_util.cc"
#include "rocksdb/util/transformrep.cc"
#include "rocksdb/util/vectorrep.cc"

#include "rocksdb/port/port_posix.cc"

#endif
