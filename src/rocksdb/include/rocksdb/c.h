/*  Copyright (c) 2013, Facebook, Inc.  All rights reserved.
  This source code is licensed under the BSD-style license found in the
  LICENSE file in the root directory of this source tree. An additional grant
  of patent rights can be found in the PATENTS file in the same directory.
 Copyright (c) 2011 The LevelDB Authors. All rights reserved.
  Use of this source code is governed by a BSD-style license that can be
  found in the LICENSE file. See the AUTHORS file for names of contributors.

  C bindings for leveldb.  May be useful as a stable ABI that can be
  used by programs that keep leveldb in a shared library, or for
  a JNI api.

  Does not support:
  . getters for the option types
  . custom comparators that implement key shortening
  . capturing post-write-snapshot
  . custom iter, db, env, cache implementations using just the C bindings

  Some conventions:

  (1) We expose just opaque struct pointers and functions to clients.
  This allows us to change internal representations without having to
  recompile clients.

  (2) For simplicity, there is no equivalent to the Slice type.  Instead,
  the caller has to pass the pointer and length as separate
  arguments.

  (3) Errors are represented by a null-terminated c string.  NULL
  means no error.  All operations that can raise an error are passed
  a "char** errptr" as the last argument.  One of the following must
  be true on entry:
     *errptr == NULL
     *errptr points to a malloc()ed null-terminated error message
  On success, a leveldb routine leaves *errptr unchanged.
  On failure, leveldb frees the old value of *errptr and
  set *errptr to a malloc()ed error message.

  (4) Bools have the type unsigned char (0 == false; rest == true)

  (5) All of the pointer arguments must be non-NULL.
*/

#ifndef STORAGE_ROCKSDB_INCLUDE_C_H_
#define STORAGE_ROCKSDB_INCLUDE_C_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/* Exported types */

typedef struct rocksdb_t                 rocksdb_t;
typedef struct rocksdb_cache_t           rocksdb_cache_t;
typedef struct rocksdb_comparator_t      rocksdb_comparator_t;
typedef struct rocksdb_env_t             rocksdb_env_t;
typedef struct rocksdb_filelock_t        rocksdb_filelock_t;
typedef struct rocksdb_filterpolicy_t    rocksdb_filterpolicy_t;
typedef struct rocksdb_flushoptions_t    rocksdb_flushoptions_t;
typedef struct rocksdb_iterator_t        rocksdb_iterator_t;
typedef struct rocksdb_logger_t          rocksdb_logger_t;
typedef struct rocksdb_mergeoperator_t   rocksdb_mergeoperator_t;
typedef struct rocksdb_options_t         rocksdb_options_t;
typedef struct rocksdb_randomfile_t      rocksdb_randomfile_t;
typedef struct rocksdb_readoptions_t     rocksdb_readoptions_t;
typedef struct rocksdb_seqfile_t         rocksdb_seqfile_t;
typedef struct rocksdb_slicetransform_t  rocksdb_slicetransform_t;
typedef struct rocksdb_snapshot_t        rocksdb_snapshot_t;
typedef struct rocksdb_writablefile_t    rocksdb_writablefile_t;
typedef struct rocksdb_writebatch_t      rocksdb_writebatch_t;
typedef struct rocksdb_writeoptions_t    rocksdb_writeoptions_t;
typedef struct rocksdb_universal_compaction_options_t rocksdb_universal_compaction_options_t;
typedef struct rocksdb_livefiles_t     rocksdb_livefiles_t;

/* DB operations */

extern rocksdb_t* rocksdb_open(
    const rocksdb_options_t* options,
    const char* name,
    char** errptr);

extern rocksdb_t* rocksdb_open_for_read_only(
    const rocksdb_options_t* options,
    const char* name,
    unsigned char error_if_log_file_exist,
    char** errptr);

extern void rocksdb_close(rocksdb_t* db);

extern void rocksdb_put(
    rocksdb_t* db,
    const rocksdb_writeoptions_t* options,
    const char* key, size_t keylen,
    const char* val, size_t vallen,
    char** errptr);

extern void rocksdb_delete(
    rocksdb_t* db,
    const rocksdb_writeoptions_t* options,
    const char* key, size_t keylen,
    char** errptr);

extern void rocksdb_merge(
    rocksdb_t* db,
    const rocksdb_writeoptions_t* options,
    const char* key, size_t keylen,
    const char* val, size_t vallen,
    char** errptr);

extern void rocksdb_write(
    rocksdb_t* db,
    const rocksdb_writeoptions_t* options,
    rocksdb_writebatch_t* batch,
    char** errptr);

/* Returns NULL if not found.  A malloc()ed array otherwise.
   Stores the length of the array in *vallen. */
extern char* rocksdb_get(
    rocksdb_t* db,
    const rocksdb_readoptions_t* options,
    const char* key, size_t keylen,
    size_t* vallen,
    char** errptr);

extern rocksdb_iterator_t* rocksdb_create_iterator(
    rocksdb_t* db,
    const rocksdb_readoptions_t* options);

extern const rocksdb_snapshot_t* rocksdb_create_snapshot(
    rocksdb_t* db);

extern void rocksdb_release_snapshot(
    rocksdb_t* db,
    const rocksdb_snapshot_t* snapshot);

/* Returns NULL if property name is unknown.
   Else returns a pointer to a malloc()-ed null-terminated value. */
extern char* rocksdb_property_value(
    rocksdb_t* db,
    const char* propname);

extern void rocksdb_approximate_sizes(
    rocksdb_t* db,
    int num_ranges,
    const char* const* range_start_key, const size_t* range_start_key_len,
    const char* const* range_limit_key, const size_t* range_limit_key_len,
    uint64_t* sizes);

extern void rocksdb_compact_range(
    rocksdb_t* db,
    const char* start_key, size_t start_key_len,
    const char* limit_key, size_t limit_key_len);

extern void rocksdb_delete_file(
    rocksdb_t* db,
    const char* name);

extern const rocksdb_livefiles_t* rocksdb_livefiles(
    rocksdb_t* db);

extern void rocksdb_flush(
    rocksdb_t* db,
    const rocksdb_flushoptions_t* options,
    char** errptr);

extern void rocksdb_disable_file_deletions(
    rocksdb_t* db,
    char** errptr);

extern void rocksdb_enable_file_deletions(
    rocksdb_t* db,
    unsigned char force,
    char** errptr);

/* Management operations */

extern void rocksdb_destroy_db(
    const rocksdb_options_t* options,
    const char* name,
    char** errptr);

extern void rocksdb_repair_db(
    const rocksdb_options_t* options,
    const char* name,
    char** errptr);

/* Iterator */

extern void rocksdb_iter_destroy(rocksdb_iterator_t*);
extern unsigned char rocksdb_iter_valid(const rocksdb_iterator_t*);
extern void rocksdb_iter_seek_to_first(rocksdb_iterator_t*);
extern void rocksdb_iter_seek_to_last(rocksdb_iterator_t*);
extern void rocksdb_iter_seek(rocksdb_iterator_t*, const char* k, size_t klen);
extern void rocksdb_iter_next(rocksdb_iterator_t*);
extern void rocksdb_iter_prev(rocksdb_iterator_t*);
extern const char* rocksdb_iter_key(const rocksdb_iterator_t*, size_t* klen);
extern const char* rocksdb_iter_value(const rocksdb_iterator_t*, size_t* vlen);
extern void rocksdb_iter_get_error(const rocksdb_iterator_t*, char** errptr);

/* Write batch */

extern rocksdb_writebatch_t* rocksdb_writebatch_create();
extern void rocksdb_writebatch_destroy(rocksdb_writebatch_t*);
extern void rocksdb_writebatch_clear(rocksdb_writebatch_t*);
extern int rocksdb_writebatch_count(rocksdb_writebatch_t*);
extern void rocksdb_writebatch_put(
    rocksdb_writebatch_t*,
    const char* key, size_t klen,
    const char* val, size_t vlen);
extern void rocksdb_writebatch_merge(
    rocksdb_writebatch_t*,
    const char* key, size_t klen,
    const char* val, size_t vlen);
extern void rocksdb_writebatch_delete(
    rocksdb_writebatch_t*,
    const char* key, size_t klen);
extern void rocksdb_writebatch_iterate(
    rocksdb_writebatch_t*,
    void* state,
    void (*put)(void*, const char* k, size_t klen, const char* v, size_t vlen),
    void (*deleted)(void*, const char* k, size_t klen));
extern const char* rocksdb_writebatch_data(rocksdb_writebatch_t*, size_t *size);

/* Options */

extern rocksdb_options_t* rocksdb_options_create();
extern void rocksdb_options_destroy(rocksdb_options_t*);
extern void rocksdb_options_set_comparator(
    rocksdb_options_t*,
    rocksdb_comparator_t*);
extern void rocksdb_options_set_merge_operator(rocksdb_options_t*,
                                               rocksdb_mergeoperator_t*);
extern void rocksdb_options_set_compression_per_level(
  rocksdb_options_t* opt,
  int* level_values,
  size_t num_levels);
extern void rocksdb_options_set_filter_policy(
    rocksdb_options_t*,
    rocksdb_filterpolicy_t*);
extern void rocksdb_options_set_create_if_missing(
    rocksdb_options_t*, unsigned char);
extern void rocksdb_options_set_error_if_exists(
    rocksdb_options_t*, unsigned char);
extern void rocksdb_options_set_paranoid_checks(
    rocksdb_options_t*, unsigned char);
extern void rocksdb_options_set_env(rocksdb_options_t*, rocksdb_env_t*);
extern void rocksdb_options_set_info_log(rocksdb_options_t*, rocksdb_logger_t*);
extern void rocksdb_options_set_info_log_level(rocksdb_options_t*, int);
extern void rocksdb_options_set_write_buffer_size(rocksdb_options_t*, size_t);
extern void rocksdb_options_set_max_open_files(rocksdb_options_t*, int);
extern void rocksdb_options_set_cache(rocksdb_options_t*, rocksdb_cache_t*);
extern void rocksdb_options_set_cache_compressed(rocksdb_options_t*, rocksdb_cache_t*);
extern void rocksdb_options_set_block_size(rocksdb_options_t*, size_t);
extern void rocksdb_options_set_block_restart_interval(rocksdb_options_t*, int);
extern void rocksdb_options_set_compression_options(
    rocksdb_options_t*, int, int, int);
extern void rocksdb_options_set_whole_key_filtering(rocksdb_options_t*, unsigned char);
extern void rocksdb_options_set_prefix_extractor(
    rocksdb_options_t*, rocksdb_slicetransform_t*);
extern void rocksdb_options_set_num_levels(rocksdb_options_t*, int);
extern void rocksdb_options_set_level0_file_num_compaction_trigger(
    rocksdb_options_t*, int);
extern void rocksdb_options_set_level0_slowdown_writes_trigger(
    rocksdb_options_t*, int);
extern void rocksdb_options_set_level0_stop_writes_trigger(
    rocksdb_options_t*, int);
extern void rocksdb_options_set_max_mem_compaction_level(
    rocksdb_options_t*, int);
extern void rocksdb_options_set_target_file_size_base(
    rocksdb_options_t*, uint64_t);
extern void rocksdb_options_set_target_file_size_multiplier(
    rocksdb_options_t*, int);
extern void rocksdb_options_set_max_bytes_for_level_base(
    rocksdb_options_t*, uint64_t);
extern void rocksdb_options_set_max_bytes_for_level_multiplier(
    rocksdb_options_t*, int);
extern void rocksdb_options_set_expanded_compaction_factor(
    rocksdb_options_t*, int);
extern void rocksdb_options_set_max_grandparent_overlap_factor(
    rocksdb_options_t*, int);
extern void rocksdb_options_set_max_bytes_for_level_multiplier_additional(
    rocksdb_options_t*, int* level_values, size_t num_levels);
extern void rocksdb_options_enable_statistics(rocksdb_options_t*);

extern void rocksdb_options_set_max_write_buffer_number(rocksdb_options_t*, int);
extern void rocksdb_options_set_min_write_buffer_number_to_merge(rocksdb_options_t*, int);
extern void rocksdb_options_set_max_background_compactions(rocksdb_options_t*, int);
extern void rocksdb_options_set_max_background_flushes(rocksdb_options_t*, int);
extern void rocksdb_options_set_max_log_file_size(rocksdb_options_t*, size_t);
extern void rocksdb_options_set_log_file_time_to_roll(rocksdb_options_t*, size_t);
extern void rocksdb_options_set_keep_log_file_num(rocksdb_options_t*, size_t);
extern void rocksdb_options_set_soft_rate_limit(rocksdb_options_t*, double);
extern void rocksdb_options_set_hard_rate_limit(rocksdb_options_t*, double);
extern void rocksdb_options_set_rate_limit_delay_max_milliseconds(
    rocksdb_options_t*, unsigned int);
extern void rocksdb_options_set_max_manifest_file_size(
    rocksdb_options_t*, size_t);
extern void rocksdb_options_set_no_block_cache(
    rocksdb_options_t*, unsigned char);
extern void rocksdb_options_set_table_cache_numshardbits(
    rocksdb_options_t*, int);
extern void rocksdb_options_set_table_cache_remove_scan_count_limit(
    rocksdb_options_t*, int);
extern void rocksdb_options_set_arena_block_size(
    rocksdb_options_t*, size_t);
extern void rocksdb_options_set_use_fsync(
    rocksdb_options_t*, int);
extern void rocksdb_options_set_db_stats_log_interval(
    rocksdb_options_t*, int);
extern void rocksdb_options_set_db_log_dir(
    rocksdb_options_t*, const char*);
extern void rocksdb_options_set_wal_dir(
    rocksdb_options_t*, const char*);
extern void rocksdb_options_set_WAL_ttl_seconds(
    rocksdb_options_t*, uint64_t);
extern void rocksdb_options_set_WAL_size_limit_MB(
    rocksdb_options_t*, uint64_t);
extern void rocksdb_options_set_manifest_preallocation_size(
    rocksdb_options_t*, size_t);
extern void rocksdb_options_set_purge_redundant_kvs_while_flush(
    rocksdb_options_t*, unsigned char);
extern void rocksdb_options_set_allow_os_buffer(
    rocksdb_options_t*, unsigned char);
extern void rocksdb_options_set_allow_mmap_reads(
    rocksdb_options_t*, unsigned char);
extern void rocksdb_options_set_allow_mmap_writes(
    rocksdb_options_t*, unsigned char);
extern void rocksdb_options_set_is_fd_close_on_exec(
    rocksdb_options_t*, unsigned char);
extern void rocksdb_options_set_skip_log_error_on_recovery(
    rocksdb_options_t*, unsigned char);
extern void rocksdb_options_set_stats_dump_period_sec(
    rocksdb_options_t*, unsigned int);
extern void rocksdb_options_set_block_size_deviation(
    rocksdb_options_t*, int);
extern void rocksdb_options_set_advise_random_on_open(
    rocksdb_options_t*, unsigned char);
extern void rocksdb_options_set_access_hint_on_compaction_start(
    rocksdb_options_t*, int);
extern void rocksdb_options_set_use_adaptive_mutex(
    rocksdb_options_t*, unsigned char);
extern void rocksdb_options_set_bytes_per_sync(
    rocksdb_options_t*, uint64_t);
extern void rocksdb_options_set_verify_checksums_in_compaction(
    rocksdb_options_t*, unsigned char);
extern void rocksdb_options_set_filter_deletes(
    rocksdb_options_t*, unsigned char);
extern void rocksdb_options_set_max_sequential_skip_in_iterations(
    rocksdb_options_t*, uint64_t);
extern void rocksdb_options_set_disable_data_sync(rocksdb_options_t*, int);
extern void rocksdb_options_set_disable_auto_compactions(rocksdb_options_t*, int);
extern void rocksdb_options_set_disable_seek_compaction(rocksdb_options_t*, int);
extern void rocksdb_options_set_delete_obsolete_files_period_micros(
    rocksdb_options_t*, uint64_t);
extern void rocksdb_options_set_source_compaction_factor(rocksdb_options_t*, int);
extern void rocksdb_options_prepare_for_bulk_load(rocksdb_options_t*);
extern void rocksdb_options_set_memtable_vector_rep(rocksdb_options_t*);
extern void rocksdb_options_set_hash_skip_list_rep(rocksdb_options_t*, size_t, int32_t, int32_t);
extern void rocksdb_options_set_hash_link_list_rep(rocksdb_options_t*, size_t);
extern void rocksdb_options_set_plain_table_factory(rocksdb_options_t*, uint32_t, int, double, size_t);

extern void rocksdb_options_set_max_bytes_for_level_base(rocksdb_options_t* opt, uint64_t n);
extern void rocksdb_options_set_stats_dump_period_sec(rocksdb_options_t* opt, unsigned int sec);

extern void rocksdb_options_set_min_level_to_compress(rocksdb_options_t* opt, int level);

extern void rocksdb_options_set_memtable_prefix_bloom_bits(
    rocksdb_options_t*, uint32_t);
extern void rocksdb_options_set_memtable_prefix_bloom_probes(
    rocksdb_options_t*, uint32_t);
extern void rocksdb_options_set_max_successive_merges(
    rocksdb_options_t*, size_t);
extern void rocksdb_options_set_min_partial_merge_operands(
    rocksdb_options_t*, uint32_t);
extern void rocksdb_options_set_bloom_locality(
    rocksdb_options_t*, uint32_t);
extern void rocksdb_options_set_allow_thread_local(
    rocksdb_options_t*, unsigned char);
extern void rocksdb_options_set_inplace_update_support(
    rocksdb_options_t*, unsigned char);
extern void rocksdb_options_set_inplace_update_num_locks(
    rocksdb_options_t*, size_t);

enum {
  rocksdb_no_compression = 0,
  rocksdb_snappy_compression = 1,
  rocksdb_zlib_compression = 2,
  rocksdb_bz2_compression = 3,
  rocksdb_lz4_compression = 4,
  rocksdb_lz4hc_compression = 5
};
extern void rocksdb_options_set_compression(rocksdb_options_t*, int);

enum {
  rocksdb_level_compaction = 0,
  rocksdb_universal_compaction = 1
};
extern void rocksdb_options_set_compaction_style(rocksdb_options_t*, int);
extern void rocksdb_options_set_universal_compaction_options(rocksdb_options_t*, rocksdb_universal_compaction_options_t*);
/* Comparator */

extern rocksdb_comparator_t* rocksdb_comparator_create(
    void* state,
    void (*destructor)(void*),
    int (*compare)(
        void*,
        const char* a, size_t alen,
        const char* b, size_t blen),
    const char* (*name)(void*));
extern void rocksdb_comparator_destroy(rocksdb_comparator_t*);

/* Filter policy */

extern rocksdb_filterpolicy_t* rocksdb_filterpolicy_create(
    void* state,
    void (*destructor)(void*),
    char* (*create_filter)(
        void*,
        const char* const* key_array, const size_t* key_length_array,
        int num_keys,
        size_t* filter_length),
    unsigned char (*key_may_match)(
        void*,
        const char* key, size_t length,
        const char* filter, size_t filter_length),
    void (*delete_filter)(
        void*,
        const char* filter, size_t filter_length),
    const char* (*name)(void*));
extern void rocksdb_filterpolicy_destroy(rocksdb_filterpolicy_t*);

extern rocksdb_filterpolicy_t* rocksdb_filterpolicy_create_bloom(
    int bits_per_key);

/* Merge Operator */

extern rocksdb_mergeoperator_t* rocksdb_mergeoperator_create(
    void* state,
    void (*destructor)(void*),
    char* (*full_merge)(
        void*,
        const char* key, size_t key_length,
        const char* existing_value, size_t existing_value_length,
        const char* const* operands_list, const size_t* operands_list_length,
        int num_operands,
        unsigned char* success, size_t* new_value_length),
    char* (*partial_merge)(
        void*,
        const char* key, size_t key_length,
        const char* const* operands_list, const size_t* operands_list_length,
        int num_operands,
        unsigned char* success, size_t* new_value_length),
    void (*delete_value)(
        void*,
        const char* value, size_t value_length),
    const char* (*name)(void*));
extern void rocksdb_mergeoperator_destroy(rocksdb_mergeoperator_t*);

/* Read options */

extern rocksdb_readoptions_t* rocksdb_readoptions_create();
extern void rocksdb_readoptions_destroy(rocksdb_readoptions_t*);
extern void rocksdb_readoptions_set_verify_checksums(
    rocksdb_readoptions_t*,
    unsigned char);
extern void rocksdb_readoptions_set_fill_cache(
    rocksdb_readoptions_t*, unsigned char);
extern void rocksdb_readoptions_set_snapshot(
    rocksdb_readoptions_t*,
    const rocksdb_snapshot_t*);
extern void rocksdb_readoptions_set_read_tier(
    rocksdb_readoptions_t*, int);
extern void rocksdb_readoptions_set_tailing(
    rocksdb_readoptions_t*, unsigned char);

/* Write options */

extern rocksdb_writeoptions_t* rocksdb_writeoptions_create();
extern void rocksdb_writeoptions_destroy(rocksdb_writeoptions_t*);
extern void rocksdb_writeoptions_set_sync(
    rocksdb_writeoptions_t*, unsigned char);
extern void rocksdb_writeoptions_disable_WAL(rocksdb_writeoptions_t* opt, int disable);

/* Flush options */

extern rocksdb_flushoptions_t* rocksdb_flushoptions_create();
extern void rocksdb_flushoptions_destroy(rocksdb_flushoptions_t*);
extern void rocksdb_flushoptions_set_wait(
    rocksdb_flushoptions_t*, unsigned char);

/* Cache */

extern rocksdb_cache_t* rocksdb_cache_create_lru(size_t capacity);
extern void rocksdb_cache_destroy(rocksdb_cache_t* cache);

/* Env */

extern rocksdb_env_t* rocksdb_create_default_env();
extern void rocksdb_env_set_background_threads(rocksdb_env_t* env, int n);
extern void rocksdb_env_set_high_priority_background_threads(rocksdb_env_t* env, int n);
extern void rocksdb_env_destroy(rocksdb_env_t*);

/* SliceTransform */

extern rocksdb_slicetransform_t* rocksdb_slicetransform_create(
    void* state,
    void (*destructor)(void*),
    char* (*transform)(
        void*,
        const char* key, size_t length,
        size_t* dst_length),
    unsigned char (*in_domain)(
        void*,
        const char* key, size_t length),
    unsigned char (*in_range)(
        void*,
        const char* key, size_t length),
    const char* (*name)(void*));
extern rocksdb_slicetransform_t* rocksdb_slicetransform_create_fixed_prefix(size_t);
extern void rocksdb_slicetransform_destroy(rocksdb_slicetransform_t*);

/* Universal Compaction options */

enum {
  rocksdb_similar_size_compaction_stop_style = 0,
  rocksdb_total_size_compaction_stop_style = 1
};

extern rocksdb_universal_compaction_options_t* rocksdb_universal_compaction_options_create() ;
extern void rocksdb_universal_compaction_options_set_size_ratio(
  rocksdb_universal_compaction_options_t*, int);
extern void rocksdb_universal_compaction_options_set_min_merge_width(
  rocksdb_universal_compaction_options_t*, int);
extern void rocksdb_universal_compaction_options_set_max_merge_width(
  rocksdb_universal_compaction_options_t*, int);
extern void rocksdb_universal_compaction_options_set_max_size_amplification_percent(
  rocksdb_universal_compaction_options_t*, int);
extern void rocksdb_universal_compaction_options_set_compression_size_percent(
  rocksdb_universal_compaction_options_t*, int);
extern void rocksdb_universal_compaction_options_set_stop_style(
  rocksdb_universal_compaction_options_t*, int);
extern void rocksdb_universal_compaction_options_destroy(
  rocksdb_universal_compaction_options_t*);

extern int rocksdb_livefiles_count(
  const rocksdb_livefiles_t*);
extern const char* rocksdb_livefiles_name(
  const rocksdb_livefiles_t*,
  int index);
extern int rocksdb_livefiles_level(
  const rocksdb_livefiles_t*,
  int index);
extern size_t rocksdb_livefiles_size(
  const rocksdb_livefiles_t*,
  int index);
extern const char* rocksdb_livefiles_smallestkey(
  const rocksdb_livefiles_t*,
  int index,
  size_t* size);
extern const char* rocksdb_livefiles_largestkey(
  const rocksdb_livefiles_t*,
  int index,
  size_t* size);
extern void rocksdb_livefiles_destroy(
  const rocksdb_livefiles_t*);

#ifdef __cplusplus
}  /* end extern "C" */
#endif

#endif  /* STORAGE_ROCKSDB_INCLUDE_C_H_ */
