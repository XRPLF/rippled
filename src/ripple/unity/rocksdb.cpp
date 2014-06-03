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

#include <BeastConfig.h>

#include <ripple/unity/rocksdb.h>

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

#include <ripple/rocksdb/rocksdb/db/builder.cc>
#include <ripple/rocksdb/rocksdb/db/db_filesnapshot.cc>
#include <ripple/rocksdb/rocksdb/db/dbformat.cc>
#include <ripple/rocksdb/rocksdb/db/db_impl.cc>
#include <ripple/rocksdb/rocksdb/db/db_impl_readonly.cc>
#include <ripple/rocksdb/rocksdb/db/db_iter.cc>
#include <ripple/rocksdb/rocksdb/db/db_stats_logger.cc>
#include <ripple/rocksdb/rocksdb/db/filename.cc>
#include <ripple/rocksdb/rocksdb/db/log_reader.cc>
#include <ripple/rocksdb/rocksdb/db/log_writer.cc>
#include <ripple/rocksdb/rocksdb/db/memtable.cc>
#include <ripple/rocksdb/rocksdb/db/memtablelist.cc>
#include <ripple/rocksdb/rocksdb/db/merge_helper.cc>
#include <ripple/rocksdb/rocksdb/db/merge_operator.cc>
#include <ripple/rocksdb/rocksdb/db/repair.cc>
#include <ripple/rocksdb/rocksdb/db/table_cache.cc>
#include <ripple/rocksdb/rocksdb/db/table_properties_collector.cc>
#include <ripple/rocksdb/rocksdb/db/transaction_log_impl.cc>
#include <ripple/rocksdb/rocksdb/db/version_edit.cc>
#include <ripple/rocksdb/rocksdb/db/version_set.cc>
#include <ripple/rocksdb/rocksdb/db/version_set_reduce_num_levels.cc>
#include <ripple/rocksdb/rocksdb/db/write_batch.cc>

#include <ripple/rocksdb/rocksdb/table/block_based_table_builder.cc>
#include <ripple/rocksdb/rocksdb/table/block_based_table_factory.cc>
#include <ripple/rocksdb/rocksdb/table/block_based_table_reader.cc>
#include <ripple/rocksdb/rocksdb/table/block_builder.cc>
#include <ripple/rocksdb/rocksdb/table/block.cc>
#include <ripple/rocksdb/rocksdb/table/filter_block.cc>
#include <ripple/rocksdb/rocksdb/table/flush_block_policy.cc>
#include <ripple/rocksdb/rocksdb/table/format.cc>
#include <ripple/rocksdb/rocksdb/table/iterator.cc>
#include <ripple/rocksdb/rocksdb/table/merger.cc>
#include <ripple/rocksdb/rocksdb/table/two_level_iterator.cc>

#include <ripple/rocksdb/rocksdb/util/arena_impl.cc>
#include <ripple/rocksdb/rocksdb/util/auto_roll_logger.cc>
#include <ripple/rocksdb/rocksdb/util/blob_store.cc>
#include <ripple/rocksdb/rocksdb/util/bloom.cc>
#include <ripple/rocksdb/rocksdb/util/cache.cc>
#include <ripple/rocksdb/rocksdb/util/coding.cc>
#include <ripple/rocksdb/rocksdb/util/comparator.cc>
#include <ripple/rocksdb/rocksdb/util/crc32c.cc>
#include <ripple/rocksdb/rocksdb/util/env.cc>
//#include "rocksdb/util/env_hdfs.cc"
#include <ripple/rocksdb/rocksdb/util/env_posix.cc>
#include <ripple/rocksdb/rocksdb/util/filter_policy.cc>
#include <ripple/rocksdb/rocksdb/util/hash.cc>
#include <ripple/rocksdb/rocksdb/util/hash_skiplist_rep.cc>
#include <ripple/rocksdb/rocksdb/util/histogram.cc>
#include <ripple/rocksdb/rocksdb/util/logging.cc>
#include <ripple/rocksdb/rocksdb/util/murmurhash.cc>
#include <ripple/rocksdb/rocksdb/util/options.cc>
#include <ripple/rocksdb/rocksdb/util/perf_context.cc>
#include <ripple/rocksdb/rocksdb/util/skiplistrep.cc>
#include <ripple/rocksdb/rocksdb/util/slice.cc>
#include <ripple/rocksdb/rocksdb/util/statistics.cc>
#include <ripple/rocksdb/rocksdb/util/status.cc>
#include <ripple/rocksdb/rocksdb/util/string_util.cc>
#include <ripple/rocksdb/rocksdb/util/transformrep.cc>
#include <ripple/rocksdb/rocksdb/util/vectorrep.cc>

#include <ripple/rocksdb/rocksdb/port/port_posix.cc>

#endif
