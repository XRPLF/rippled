cmake_minimum_required (VERSION 3.2)

GroupSources(bench /)
GroupSources(include/nudb nudb)
GroupSources(extras/nudb extras)
GroupSources(extras/beast/include/beast beast)
GroupSources(extras/beast/extras/beast beast)
GroupSources(extras/rocksdb rocksdb)

if (WIN32)
  set(CMAKE_CONFIGURATION_TYPES Release)
endif ()

project (bench)

############################################################

macro(append_flags name)
  foreach (arg ${ARGN})
    set(${name} "${${name}} ${arg}")
  endforeach()
endmacro()

############################################################

set (DEPS "${PROJECT_SOURCE_DIR}/../extras")

set (DEFAULT_WITH_ROCKSDB true)

set (WITH_ROCKSDB ${DEFAULT_WITH_ROCKSDB} CACHE BOOL "Runs benchmarks against rocksdb")

if (WITH_ROCKSDB)
  set(ROCKSDB ${DEPS}/rocksdb)

  set(ROCKSDB_SRC
    ${ROCKSDB}/db/auto_roll_logger.cc
    ${ROCKSDB}/db/builder.cc
    ${ROCKSDB}/db/c.cc
    ${ROCKSDB}/db/column_family.cc
    ${ROCKSDB}/db/compacted_db_impl.cc
    ${ROCKSDB}/db/compaction.cc
    ${ROCKSDB}/db/compaction_iterator.cc
    ${ROCKSDB}/db/compaction_job.cc
    ${ROCKSDB}/db/compaction_picker.cc
    ${ROCKSDB}/db/convenience.cc
    ${ROCKSDB}/db/db_filesnapshot.cc
    ${ROCKSDB}/db/dbformat.cc
    ${ROCKSDB}/db/db_impl.cc
    ${ROCKSDB}/db/db_impl_debug.cc
    ${ROCKSDB}/db/db_impl_readonly.cc
    ${ROCKSDB}/db/db_impl_experimental.cc
    ${ROCKSDB}/db/db_impl_add_file.cc
    ${ROCKSDB}/db/db_info_dumper.cc
    ${ROCKSDB}/db/db_iter.cc
    ${ROCKSDB}/db/experimental.cc
    ${ROCKSDB}/db/event_helpers.cc
    ${ROCKSDB}/db/file_indexer.cc
    ${ROCKSDB}/db/filename.cc
    ${ROCKSDB}/db/flush_job.cc
    ${ROCKSDB}/db/flush_scheduler.cc
    ${ROCKSDB}/db/forward_iterator.cc
    ${ROCKSDB}/db/internal_stats.cc
    ${ROCKSDB}/db/log_reader.cc
    ${ROCKSDB}/db/log_writer.cc
    ${ROCKSDB}/db/managed_iterator.cc
    ${ROCKSDB}/db/memtable_allocator.cc
    ${ROCKSDB}/db/memtable.cc
    ${ROCKSDB}/db/memtable_list.cc
    ${ROCKSDB}/db/merge_helper.cc
    ${ROCKSDB}/db/merge_operator.cc
    ${ROCKSDB}/db/repair.cc
    ${ROCKSDB}/db/snapshot_impl.cc
    ${ROCKSDB}/db/table_cache.cc
    ${ROCKSDB}/db/table_properties_collector.cc
    ${ROCKSDB}/db/transaction_log_impl.cc
    ${ROCKSDB}/db/version_builder.cc
    ${ROCKSDB}/db/version_edit.cc
    ${ROCKSDB}/db/version_set.cc
    ${ROCKSDB}/db/wal_manager.cc
    ${ROCKSDB}/db/write_batch.cc
    ${ROCKSDB}/db/write_batch_base.cc
    ${ROCKSDB}/db/write_controller.cc
    ${ROCKSDB}/db/write_thread.cc
    ${ROCKSDB}/db/xfunc_test_points.cc
    ${ROCKSDB}/memtable/hash_cuckoo_rep.cc
    ${ROCKSDB}/memtable/hash_linklist_rep.cc
    ${ROCKSDB}/memtable/hash_skiplist_rep.cc
    ${ROCKSDB}/memtable/skiplistrep.cc
    ${ROCKSDB}/memtable/vectorrep.cc
    ${ROCKSDB}/port/stack_trace.cc
    ${ROCKSDB}/table/adaptive_table_factory.cc
    ${ROCKSDB}/table/block_based_filter_block.cc
    ${ROCKSDB}/table/block_based_table_builder.cc
    ${ROCKSDB}/table/block_based_table_factory.cc
    ${ROCKSDB}/table/block_based_table_reader.cc
    ${ROCKSDB}/table/block_builder.cc
    ${ROCKSDB}/table/block.cc
    ${ROCKSDB}/table/block_prefix_index.cc
    ${ROCKSDB}/table/bloom_block.cc
    ${ROCKSDB}/table/cuckoo_table_builder.cc
    ${ROCKSDB}/table/cuckoo_table_factory.cc
    ${ROCKSDB}/table/cuckoo_table_reader.cc
    ${ROCKSDB}/table/flush_block_policy.cc
    ${ROCKSDB}/table/format.cc
    ${ROCKSDB}/table/full_filter_block.cc
    ${ROCKSDB}/table/get_context.cc
    ${ROCKSDB}/table/iterator.cc
    ${ROCKSDB}/table/merger.cc
    ${ROCKSDB}/table/meta_blocks.cc
    ${ROCKSDB}/table/sst_file_writer.cc
    ${ROCKSDB}/table/plain_table_builder.cc
    ${ROCKSDB}/table/plain_table_factory.cc
    ${ROCKSDB}/table/plain_table_index.cc
    ${ROCKSDB}/table/plain_table_key_coding.cc
    ${ROCKSDB}/table/plain_table_reader.cc
    ${ROCKSDB}/table/persistent_cache_helper.cc
    ${ROCKSDB}/table/table_properties.cc
    ${ROCKSDB}/table/two_level_iterator.cc
    ${ROCKSDB}/tools/dump/db_dump_tool.cc
    ${ROCKSDB}/util/arena.cc
    ${ROCKSDB}/util/bloom.cc
    # ${ROCKSDB}/util/build_version.cc
    ${ROCKSDB}/util/coding.cc
    ${ROCKSDB}/util/comparator.cc
    ${ROCKSDB}/util/compaction_job_stats_impl.cc
    ${ROCKSDB}/util/concurrent_arena.cc
    ${ROCKSDB}/util/crc32c.cc
    ${ROCKSDB}/util/delete_scheduler.cc
    ${ROCKSDB}/util/dynamic_bloom.cc
    ${ROCKSDB}/util/env.cc
    ${ROCKSDB}/util/env_chroot.cc
    ${ROCKSDB}/util/env_hdfs.cc
    ${ROCKSDB}/util/file_util.cc
    ${ROCKSDB}/util/file_reader_writer.cc
    ${ROCKSDB}/util/filter_policy.cc
    ${ROCKSDB}/util/hash.cc
    ${ROCKSDB}/util/histogram.cc
    ${ROCKSDB}/util/histogram_windowing.cc
    ${ROCKSDB}/util/instrumented_mutex.cc
    ${ROCKSDB}/util/iostats_context.cc
    ${ROCKSDB}/util/lru_cache.cc
    ${ROCKSDB}/util/threadpool.cc
    ${ROCKSDB}/util/transaction_test_util.cc
    ${ROCKSDB}/util/sharded_cache.cc
    ${ROCKSDB}/util/sst_file_manager_impl.cc
    ${ROCKSDB}/utilities/backupable/backupable_db.cc
    ${ROCKSDB}/utilities/blob_db/blob_db.cc
    ${ROCKSDB}/utilities/convenience/info_log_finder.cc
    ${ROCKSDB}/utilities/checkpoint/checkpoint.cc
    ${ROCKSDB}/utilities/compaction_filters/remove_emptyvalue_compactionfilter.cc
    ${ROCKSDB}/utilities/document/document_db.cc
    ${ROCKSDB}/utilities/document/json_document_builder.cc
    ${ROCKSDB}/utilities/document/json_document.cc
    ${ROCKSDB}/utilities/env_mirror.cc
    ${ROCKSDB}/utilities/env_registry.cc
    ${ROCKSDB}/utilities/flashcache/flashcache.cc
    ${ROCKSDB}/utilities/geodb/geodb_impl.cc
    ${ROCKSDB}/utilities/leveldb_options/leveldb_options.cc
    ${ROCKSDB}/utilities/memory/memory_util.cc
    ${ROCKSDB}/utilities/merge_operators/put.cc
    ${ROCKSDB}/utilities/merge_operators/max.cc
    ${ROCKSDB}/utilities/merge_operators/string_append/stringappend2.cc
    ${ROCKSDB}/utilities/merge_operators/string_append/stringappend.cc
    ${ROCKSDB}/utilities/merge_operators/uint64add.cc
    ${ROCKSDB}/utilities/option_change_migration/option_change_migration.cc
    ${ROCKSDB}/utilities/options/options_util.cc
    ${ROCKSDB}/utilities/persistent_cache/persistent_cache_tier.cc
    ${ROCKSDB}/utilities/persistent_cache/volatile_tier_impl.cc
    ${ROCKSDB}/utilities/persistent_cache/block_cache_tier_file.cc
    ${ROCKSDB}/utilities/persistent_cache/block_cache_tier_metadata.cc
    ${ROCKSDB}/utilities/persistent_cache/block_cache_tier.cc
    ${ROCKSDB}/utilities/redis/redis_lists.cc
    ${ROCKSDB}/utilities/simulator_cache/sim_cache.cc
    ${ROCKSDB}/utilities/spatialdb/spatial_db.cc
    ${ROCKSDB}/utilities/table_properties_collectors/compact_on_deletion_collector.cc
    ${ROCKSDB}/utilities/transactions/optimistic_transaction_impl.cc
    ${ROCKSDB}/utilities/transactions/optimistic_transaction_db_impl.cc
    ${ROCKSDB}/utilities/transactions/transaction_base.cc
    ${ROCKSDB}/utilities/transactions/transaction_db_impl.cc
    ${ROCKSDB}/utilities/transactions/transaction_db_mutex_impl.cc
    ${ROCKSDB}/utilities/transactions/transaction_lock_mgr.cc
    ${ROCKSDB}/utilities/transactions/transaction_impl.cc
    ${ROCKSDB}/utilities/transactions/transaction_util.cc
    ${ROCKSDB}/utilities/ttl/db_ttl_impl.cc
    ${ROCKSDB}/utilities/date_tiered/date_tiered_db_impl.cc
    ${ROCKSDB}/utilities/write_batch_with_index/write_batch_with_index.cc
    ${ROCKSDB}/utilities/write_batch_with_index/write_batch_with_index_internal.cc
    ${ROCKSDB}/util/event_logger.cc
    ${ROCKSDB}/util/log_buffer.cc
    ${ROCKSDB}/util/logging.cc
    ${ROCKSDB}/util/memenv.cc
    ${ROCKSDB}/util/murmurhash.cc
    ${ROCKSDB}/util/mutable_cf_options.cc
    ${ROCKSDB}/util/options.cc
    ${ROCKSDB}/util/options_helper.cc
    ${ROCKSDB}/util/options_parser.cc
    ${ROCKSDB}/util/options_sanity_check.cc
    ${ROCKSDB}/util/perf_context.cc
    ${ROCKSDB}/util/perf_level.cc
    ${ROCKSDB}/util/random.cc
    ${ROCKSDB}/util/rate_limiter.cc
    ${ROCKSDB}/util/slice.cc
    ${ROCKSDB}/util/statistics.cc
    ${ROCKSDB}/util/status.cc
    ${ROCKSDB}/util/status_message.cc
    ${ROCKSDB}/util/string_util.cc
    ${ROCKSDB}/util/sync_point.cc
    ${ROCKSDB}/util/thread_local.cc
    ${ROCKSDB}/util/thread_status_impl.cc
    ${ROCKSDB}/util/thread_status_updater.cc
    ${ROCKSDB}/util/thread_status_updater_debug.cc
    ${ROCKSDB}/util/thread_status_util.cc
    ${ROCKSDB}/util/thread_status_util_debug.cc
    ${ROCKSDB}/util/xfunc.cc
    ${ROCKSDB}/util/xxhash.cc
    )

  if (WIN32)
    add_definitions(
      -DOS_WIN
      )
    set(ROCKSDB_SRC ${ROCKSDB_SRC}
      ${ROCKSDB}/port/win/io_win.cc
      ${ROCKSDB}/port/win/env_default.cc
      ${ROCKSDB}/port/win/env_win.cc
      ${ROCKSDB}/port/win/port_win.cc
      ${ROCKSDB}/port/win/win_logger.cc
      ${ROCKSDB}/port/win/xpress_win.cc
      )
  else ()
    #if (${CMAKE_SYSTEM_NAME} MATCHES Linux)
    add_definitions(
      -DOS_LINUX
      -DROCKSDB_PLATFORM_POSIX
      -DROCKSDB_LIB_IO_POSIX
      )
    set(ROCKSDB_SRC ${ROCKSDB_SRC}
      ${ROCKSDB}/util/io_posix.cc
      ${ROCKSDB}/util/env_posix.cc
      ${ROCKSDB}/port/port_posix.cc
      )
  endif ()

  include_directories(
    SYSTEM
    ${ROCKSDB}
    ${ROCKSDB}/include
    ${ROCKSDB}/third-party/gtest-1.7.0/fused-src
    )

  add_definitions(
    -DWITH_ROCKSDB
    )
endif(WITH_ROCKSDB)

if (NOT WIN32)
  append_flags(CMAKE_CXX_FLAGS -std=c++11)
endif ()

if(WIN32)
  add_compile_options(
    /bigobj              # Increase object file max size
    /EHa                 # ExceptionHandling all
    /fp:precise          # Floating point behavior
    /Gd                  # __cdecl calling convention
    /Gm-                 # Minimal rebuild: disabled
    /GR                  # Enable RTTI
    /Gy-                 # Function level linking: disabled
    /FS
    /MP                  # Multiprocessor compilation
    /openmp-             # pragma omp: disabled
    /Zc:forScope         # Language extension: for scope
    /Zi                  # Generate complete debug info
    /errorReport:none    # No error reporting to Internet
    /nologo              # Suppress login banner
    /W3                  # Warning level 3
    /WX-                 # Disable warnings as errors
    /wd"4018"
    /wd"4244"
    /wd"4267"
    /wd"4800"            # Disable C4800(int to bool performance)
    /wd"4503"            # Decorated name length exceeded, name was truncated
    )

  add_definitions(
    -D_WIN32_WINNT=0x6000
    -D_ITERATOR_DEBUG_LEVEL=0
    -D_SCL_SECURE_NO_WARNINGS
    -D_CRT_SECURE_NO_WARNINGS
    -DWIN32_CONSOLE
    -DNOMINMAX)

  append_flags(CMAKE_EXE_LINKER_FLAGS
    /DEBUG
    /DYNAMICBASE
    /ERRORREPORT:NONE
    /MACHINE:X64
    /MANIFEST
    /nologo
    /NXCOMPAT
    /SUBSYSTEM:CONSOLE
    /TLBID:1)

  # There seems to be an issue using generator experssions with multiple values,
  # split the expression
  # /GS  Buffers security check: enable
  add_compile_options($<$<CONFIG:Debug>:/GS>)
  # /MTd Language: Multi-threaded Debug CRT
  add_compile_options($<$<CONFIG:Debug>:/MTd>)
  # /Od  Optimization: Disabled
  add_compile_options($<$<CONFIG:Debug>:/Od>)
  # /RTC1 Run-time error checks:
  add_compile_options($<$<CONFIG:Debug>:/RTC1>)

  # Generator expressions are not supported in add_definitions, use set_property instead
  set_property(
    DIRECTORY
    APPEND
    PROPERTY COMPILE_DEFINITIONS
    $<$<CONFIG:Debug>:_CRTDBG_MAP_ALLOC>)

  # /MT Language: Multi-threaded CRT
  add_compile_options($<$<CONFIG:Release>:/MT>)
  add_compile_options($<$<CONFIG:Release>:/Ox>)
  # /Ox Optimization: Full

endif (WIN32)


include_directories(
  ../include
  ../test
  .
  ${DEPS}
  )

add_executable(bench
    ${BEAST_INCLUDES}
    ${EXTRAS_INCLUDES}
    ${NUDB_INCLUDES}
    ${ROCKSDB_SRC}
    bench.cpp
  )

target_link_libraries(bench
  ${Boost_LIBRARIES}
  )

if (WITH_ROCKSDB)
  if (WIN32)
    target_link_libraries(bench
      Rpcrt4
    )
  else ()
    target_link_libraries(bench
      rt
      Threads::Threads
      z
      snappy
    )
  endif ()
endif ()

