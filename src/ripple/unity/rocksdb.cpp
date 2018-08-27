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


#include <ripple/unity/rocksdb.h>

#if RIPPLE_ROCKSDB_AVAILABLE

// Compile RocksDB without debugging unless specifically requested
#if !defined (NDEBUG) && !defined (RIPPLE_DEBUG_ROCKSDB)
#define NDEBUG
#endif

#include <rocksdb2/cache/lru_cache.cc>
#include <rocksdb2/cache/sharded_cache.cc>
#include <rocksdb2/db/builder.cc>
#include <rocksdb2/db/c.cc>
#include <rocksdb2/db/column_family.cc>
#include <rocksdb2/db/compacted_db_impl.cc>
#include <rocksdb2/db/compaction.cc>
#include <rocksdb2/db/compaction_iterator.cc>
#include <rocksdb2/db/compaction_job.cc>
#include <rocksdb2/db/compaction_picker.cc>
#include <rocksdb2/db/compaction_picker_universal.cc>
#include <rocksdb2/db/convenience.cc>
#include <rocksdb2/db/db_filesnapshot.cc>
#include <rocksdb2/db/db_impl.cc>
#include <rocksdb2/db/db_impl_compaction_flush.cc>
#include <rocksdb2/db/db_impl_debug.cc>
#include <rocksdb2/db/db_impl_experimental.cc>
#include <rocksdb2/db/db_impl_files.cc>
#include <rocksdb2/db/db_impl_open.cc>
#include <rocksdb2/db/db_impl_readonly.cc>
#include <rocksdb2/db/db_impl_write.cc>
#include <rocksdb2/db/db_info_dumper.cc>
#include <rocksdb2/db/db_iter.cc>
#include <rocksdb2/db/dbformat.cc>
#include <rocksdb2/db/event_helpers.cc>
#include <rocksdb2/db/external_sst_file_ingestion_job.cc>
#include <rocksdb2/db/file_indexer.cc>
#include <rocksdb2/db/flush_job.cc>
#include <rocksdb2/db/flush_scheduler.cc>
#include <rocksdb2/db/forward_iterator.cc>
#include <rocksdb2/db/internal_stats.cc>
#include <rocksdb2/db/log_reader.cc>
#include <rocksdb2/db/log_writer.cc>
#include <rocksdb2/db/malloc_stats.cc>
#include <rocksdb2/db/managed_iterator.cc>
#include <rocksdb2/db/memtable.cc>
#include <rocksdb2/db/memtable_list.cc>
#include <rocksdb2/db/merge_helper.cc>
#include <rocksdb2/db/merge_operator.cc>
#include <rocksdb2/db/range_del_aggregator.cc>
#include <rocksdb2/db/repair.cc>
#include <rocksdb2/db/table_cache.cc>
#include <rocksdb2/db/table_properties_collector.cc>
#include <rocksdb2/db/transaction_log_impl.cc>
#include <rocksdb2/db/version_builder.cc>
#include <rocksdb2/db/version_edit.cc>
#include <rocksdb2/db/version_set.cc>
#include <rocksdb2/db/wal_manager.cc>
#include <rocksdb2/db/write_batch.cc>
#include <rocksdb2/db/write_batch_base.cc>
#include <rocksdb2/db/write_controller.cc>
#include <rocksdb2/db/write_thread.cc>
#include <rocksdb2/env/env.cc>
#include <rocksdb2/env/env_hdfs.cc>
#include <rocksdb2/env/env_posix.cc>
#include <rocksdb2/env/io_posix.cc>
#include <rocksdb2/env/mock_env.cc>
#include <rocksdb2/memtable/alloc_tracker.cc>
#include <rocksdb2/memtable/hash_cuckoo_rep.cc>
#include <rocksdb2/memtable/hash_linklist_rep.cc>
#include <rocksdb2/memtable/hash_skiplist_rep.cc>
#include <rocksdb2/memtable/skiplistrep.cc>
#include <rocksdb2/memtable/vectorrep.cc>
#include <rocksdb2/memtable/write_buffer_manager.cc>
#include <rocksdb2/monitoring/histogram.cc>
#include <rocksdb2/monitoring/instrumented_mutex.cc>
#include <rocksdb2/monitoring/iostats_context.cc>
#include <rocksdb2/monitoring/perf_context.cc>
#include <rocksdb2/monitoring/perf_level.cc>
#include <rocksdb2/monitoring/statistics.cc>
#include <rocksdb2/monitoring/thread_status_updater.cc>
#include <rocksdb2/monitoring/thread_status_util.cc>
#include <rocksdb2/options/cf_options.cc>
#include <rocksdb2/options/db_options.cc>
#include <rocksdb2/options/options.cc>
#include <rocksdb2/options/options_helper.cc>
#include <rocksdb2/options/options_parser.cc>
#include <rocksdb2/options/options_sanity_check.cc>
#include <rocksdb2/port/port_posix.cc>
#include <rocksdb2/port/stack_trace.cc>
#include <rocksdb2/table/adaptive_table_factory.cc>
#include <rocksdb2/table/block.cc>
#include <rocksdb2/table/block_based_filter_block.cc>
#include <rocksdb2/table/block_based_table_builder.cc>
#include <rocksdb2/table/block_based_table_factory.cc>
#include <rocksdb2/table/block_based_table_reader.cc>
#include <rocksdb2/table/block_builder.cc>
#include <rocksdb2/table/block_prefix_index.cc>
#include <rocksdb2/table/bloom_block.cc>
#include <rocksdb2/table/cuckoo_table_builder.cc>
#include <rocksdb2/table/cuckoo_table_factory.cc>
#include <rocksdb2/table/cuckoo_table_reader.cc>
#include <rocksdb2/table/flush_block_policy.cc>
#include <rocksdb2/table/format.cc>
#include <rocksdb2/table/full_filter_block.cc>
#include <rocksdb2/table/get_context.cc>
#include <rocksdb2/table/index_builder.cc>
#include <rocksdb2/table/iterator.cc>
#include <rocksdb2/table/merging_iterator.cc>
#include <rocksdb2/table/meta_blocks.cc>
#include <rocksdb2/table/partitioned_filter_block.cc>
#include <rocksdb2/table/persistent_cache_helper.cc>
#include <rocksdb2/table/plain_table_builder.cc>
#include <rocksdb2/table/plain_table_factory.cc>
#include <rocksdb2/table/plain_table_index.cc>
#include <rocksdb2/table/plain_table_key_coding.cc>
#include <rocksdb2/table/plain_table_reader.cc>
#include <rocksdb2/table/sst_file_writer.cc>
#include <rocksdb2/table/table_properties.cc>
#include <rocksdb2/table/two_level_iterator.cc>
#include <rocksdb2/util/arena.cc>
#include <rocksdb2/util/auto_roll_logger.cc>
#include <rocksdb2/util/bloom.cc>
#include <rocksdb2/util/coding.cc>
#include <rocksdb2/util/compaction_job_stats_impl.cc>
#include <rocksdb2/util/comparator.cc>
#include <rocksdb2/util/concurrent_arena.cc>
#include <rocksdb2/util/crc32c.cc>
#include <rocksdb2/util/delete_scheduler.cc>
#include <rocksdb2/util/dynamic_bloom.cc>
#include <rocksdb2/util/event_logger.cc>
#include <rocksdb2/util/file_reader_writer.cc>
#include <rocksdb2/util/file_util.cc>
#include <rocksdb2/util/filename.cc>
#include <rocksdb2/util/filter_policy.cc>
#include <rocksdb2/util/hash.cc>
#include <rocksdb2/util/log_buffer.cc>
#include <rocksdb2/util/murmurhash.cc>
#include <rocksdb2/util/random.cc>
#include <rocksdb2/util/rate_limiter.cc>
#include <rocksdb2/util/slice.cc>
#include <rocksdb2/util/sst_file_manager_impl.cc>
#include <rocksdb2/util/status.cc>
#include <rocksdb2/util/status_message.cc>
#include <rocksdb2/util/string_util.cc>
#include <rocksdb2/util/sync_point.cc>
#include <rocksdb2/util/thread_local.cc>
#include <rocksdb2/util/threadpool_imp.cc>
#include <rocksdb2/util/xxhash.cc>
#include <rocksdb2/utilities/backupable/backupable_db.cc>
#include <rocksdb2/utilities/checkpoint/checkpoint_impl.cc>
#include <rocksdb2/utilities/document/document_db.cc>
#include <rocksdb2/utilities/document/json_document.cc>
#include <rocksdb2/utilities/document/json_document_builder.cc>
#include <rocksdb2/utilities/geodb/geodb_impl.cc>
#include <rocksdb2/utilities/merge_operators/put.cc>
#include <rocksdb2/utilities/merge_operators/string_append/stringappend.cc>
#include <rocksdb2/utilities/merge_operators/string_append/stringappend2.cc>
#include <rocksdb2/utilities/merge_operators/uint64add.cc>
#include <rocksdb2/utilities/redis/redis_lists.cc>
#include <rocksdb2/utilities/spatialdb/spatial_db.cc>
#include <rocksdb2/utilities/transactions/optimistic_transaction.cc>
#include <rocksdb2/utilities/transactions/optimistic_transaction_db_impl.cc>
#include <rocksdb2/utilities/transactions/pessimistic_transaction.cc>
#include <rocksdb2/utilities/transactions/pessimistic_transaction_db.cc>
#include <rocksdb2/utilities/transactions/transaction_base.cc>
#include <rocksdb2/utilities/transactions/transaction_db_mutex_impl.cc>
#include <rocksdb2/utilities/transactions/transaction_lock_mgr.cc>
#include <rocksdb2/utilities/transactions/transaction_util.cc>
#include <rocksdb2/utilities/transactions/write_prepared_txn.cc>
#include <rocksdb2/utilities/ttl/db_ttl_impl.cc>
#include <rocksdb2/utilities/write_batch_with_index/write_batch_with_index.cc>
#include <rocksdb2/utilities/write_batch_with_index/write_batch_with_index_internal.cc>

const char* rocksdb_build_git_sha = "<none>";
const char* rocksdb_build_git_datetime = "<none>";
// Don't use __DATE__ and __TIME__, otherwise
// builds will be nondeterministic.
const char* rocksdb_build_compile_date = "Ripple Labs";
const char* rocksdb_build_compile_time = "C++ Team";
//const char* rocksdb_build_compile_date = __DATE__;
//const char* rocksdb_build_compile_time = __TIME__;

#endif
