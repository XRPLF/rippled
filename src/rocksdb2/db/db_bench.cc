//  Copyright (c) 2013, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#define __STDC_FORMAT_MACROS

#ifndef GFLAGS
#include <cstdio>
int main() {
  fprintf(stderr, "Please install gflags to run rocksdb tools\n");
  return 1;
}
#else

#ifdef NUMA
#include <numa.h>
#include <numaif.h>
#endif

#include <inttypes.h>
#include <cstddef>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <gflags/gflags.h>
#include "db/db_impl.h"
#include "db/version_set.h"
#include "rocksdb/options.h"
#include "rocksdb/cache.h"
#include "rocksdb/db.h"
#include "rocksdb/env.h"
#include "rocksdb/memtablerep.h"
#include "rocksdb/write_batch.h"
#include "rocksdb/slice.h"
#include "rocksdb/slice_transform.h"
#include "rocksdb/statistics.h"
#include "rocksdb/perf_context.h"
#include "port/port.h"
#include "port/stack_trace.h"
#include "util/crc32c.h"
#include "util/histogram.h"
#include "util/mutexlock.h"
#include "util/random.h"
#include "util/string_util.h"
#include "util/statistics.h"
#include "util/testutil.h"
#include "util/xxhash.h"
#include "hdfs/env_hdfs.h"
#include "utilities/merge_operators.h"

using GFLAGS::ParseCommandLineFlags;
using GFLAGS::RegisterFlagValidator;
using GFLAGS::SetUsageMessage;

DEFINE_string(benchmarks,
              "fillseq,"
              "fillsync,"
              "fillrandom,"
              "overwrite,"
              "readrandom,"
              "newiterator,"
              "newiteratorwhilewriting,"
              "seekrandom,"
              "seekrandomwhilewriting,"
              "readseq,"
              "readreverse,"
              "compact,"
              "readrandom,"
              "multireadrandom,"
              "readseq,"
              "readtocache,"
              "readreverse,"
              "readwhilewriting,"
              "readrandomwriterandom,"
              "updaterandom,"
              "randomwithverify,"
              "fill100K,"
              "crc32c,"
              "xxhash,"
              "compress,"
              "uncompress,"
              "acquireload,",

              "Comma-separated list of operations to run in the specified order"
              "Actual benchmarks:\n"
              "\tfillseq       -- write N values in sequential key"
              " order in async mode\n"
              "\tfillrandom    -- write N values in random key order in async"
              " mode\n"
              "\toverwrite     -- overwrite N values in random key order in"
              " async mode\n"
              "\tfillsync      -- write N/100 values in random key order in "
              "sync mode\n"
              "\tfill100K      -- write N/1000 100K values in random order in"
              " async mode\n"
              "\tdeleteseq     -- delete N keys in sequential order\n"
              "\tdeleterandom  -- delete N keys in random order\n"
              "\treadseq       -- read N times sequentially\n"
              "\treadtocache   -- 1 thread reading database sequentially\n"
              "\treadreverse   -- read N times in reverse order\n"
              "\treadrandom    -- read N times in random order\n"
              "\treadmissing   -- read N missing keys in random order\n"
              "\treadhot       -- read N times in random order from 1% section "
              "of DB\n"
              "\treadwhilewriting      -- 1 writer, N threads doing random "
              "reads\n"
              "\treadrandomwriterandom -- N threads doing random-read, "
              "random-write\n"
              "\tprefixscanrandom      -- prefix scan N times in random order\n"
              "\tupdaterandom  -- N threads doing read-modify-write for random "
              "keys\n"
              "\tappendrandom  -- N threads doing read-modify-write with "
              "growing values\n"
              "\tmergerandom   -- same as updaterandom/appendrandom using merge"
              " operator. "
              "Must be used with merge_operator\n"
              "\treadrandommergerandom -- perform N random read-or-merge "
              "operations. Must be used with merge_operator\n"
              "\tnewiterator   -- repeated iterator creation\n"
              "\tseekrandom    -- N random seeks\n"
              "\tseekrandom    -- 1 writer, N threads doing random seeks\n"
              "\tcrc32c        -- repeated crc32c of 4K of data\n"
              "\txxhash        -- repeated xxHash of 4K of data\n"
              "\tacquireload   -- load N*1000 times\n"
              "Meta operations:\n"
              "\tcompact     -- Compact the entire DB\n"
              "\tstats       -- Print DB stats\n"
              "\tlevelstats  -- Print the number of files and bytes per level\n"
              "\tsstables    -- Print sstable info\n"
              "\theapprofile -- Dump a heap profile (if supported by this"
              " port)\n");

DEFINE_int64(num, 1000000, "Number of key/values to place in database");

DEFINE_int64(numdistinct, 1000,
             "Number of distinct keys to use. Used in RandomWithVerify to "
             "read/write on fewer keys so that gets are more likely to find the"
             " key and puts are more likely to update the same key");

DEFINE_int64(merge_keys, -1,
             "Number of distinct keys to use for MergeRandom and "
             "ReadRandomMergeRandom. "
             "If negative, there will be FLAGS_num keys.");
DEFINE_int32(num_column_families, 1, "Number of Column Families to use.");

DEFINE_int64(reads, -1, "Number of read operations to do.  "
             "If negative, do FLAGS_num reads.");

DEFINE_int32(bloom_locality, 0, "Control bloom filter probes locality");

DEFINE_int64(seed, 0, "Seed base for random number generators. "
             "When 0 it is deterministic.");

DEFINE_int32(threads, 1, "Number of concurrent threads to run.");

DEFINE_int32(duration, 0, "Time in seconds for the random-ops tests to run."
             " When 0 then num & reads determine the test duration");

DEFINE_int32(value_size, 100, "Size of each value");

DEFINE_bool(use_uint64_comparator, false, "use Uint64 user comparator");

static bool ValidateKeySize(const char* flagname, int32_t value) {
  return true;
}

DEFINE_int32(key_size, 16, "size of each key");

DEFINE_int32(num_multi_db, 0,
             "Number of DBs used in the benchmark. 0 means single DB.");

DEFINE_double(compression_ratio, 0.5, "Arrange to generate values that shrink"
              " to this fraction of their original size after compression");

DEFINE_bool(histogram, false, "Print histogram of operation timings");

DEFINE_bool(enable_numa, false,
            "Make operations aware of NUMA architecture and bind memory "
            "and cpus corresponding to nodes together. In NUMA, memory "
            "in same node as CPUs are closer when compared to memory in "
            "other nodes. Reads can be faster when the process is bound to "
            "CPU and memory of same node. Use \"$numactl --hardware\" command "
            "to see NUMA memory architecture.");

DEFINE_int64(write_buffer_size, rocksdb::Options().write_buffer_size,
             "Number of bytes to buffer in memtable before compacting");

DEFINE_int32(max_write_buffer_number,
             rocksdb::Options().max_write_buffer_number,
             "The number of in-memory memtables. Each memtable is of size"
             "write_buffer_size.");

DEFINE_int32(min_write_buffer_number_to_merge,
             rocksdb::Options().min_write_buffer_number_to_merge,
             "The minimum number of write buffers that will be merged together"
             "before writing to storage. This is cheap because it is an"
             "in-memory merge. If this feature is not enabled, then all these"
             "write buffers are flushed to L0 as separate files and this "
             "increases read amplification because a get request has to check"
             " in all of these files. Also, an in-memory merge may result in"
             " writing less data to storage if there are duplicate records "
             " in each of these individual write buffers.");

DEFINE_int32(max_background_compactions,
             rocksdb::Options().max_background_compactions,
             "The maximum number of concurrent background compactions"
             " that can occur in parallel.");

DEFINE_int32(max_background_flushes,
             rocksdb::Options().max_background_flushes,
             "The maximum number of concurrent background flushes"
             " that can occur in parallel.");

static rocksdb::CompactionStyle FLAGS_compaction_style_e;
DEFINE_int32(compaction_style, (int32_t) rocksdb::Options().compaction_style,
             "style of compaction: level-based vs universal");

DEFINE_int32(universal_size_ratio, 0,
             "Percentage flexibility while comparing file size"
             " (for universal compaction only).");

DEFINE_int32(universal_min_merge_width, 0, "The minimum number of files in a"
             " single compaction run (for universal compaction only).");

DEFINE_int32(universal_max_merge_width, 0, "The max number of files to compact"
             " in universal style compaction");

DEFINE_int32(universal_max_size_amplification_percent, 0,
             "The max size amplification for universal style compaction");

DEFINE_int32(universal_compression_size_percent, -1,
             "The percentage of the database to compress for universal "
             "compaction. -1 means compress everything.");

DEFINE_int64(cache_size, -1, "Number of bytes to use as a cache of uncompressed"
             "data. Negative means use default settings.");

DEFINE_int32(block_size, rocksdb::BlockBasedTableOptions().block_size,
             "Number of bytes in a block.");

DEFINE_int32(block_restart_interval,
             rocksdb::BlockBasedTableOptions().block_restart_interval,
             "Number of keys between restart points "
             "for delta encoding of keys.");

DEFINE_int64(compressed_cache_size, -1,
             "Number of bytes to use as a cache of compressed data.");

DEFINE_int32(open_files, rocksdb::Options().max_open_files,
             "Maximum number of files to keep open at the same time"
             " (use default if == 0)");

DEFINE_int32(bloom_bits, -1, "Bloom filter bits per key. Negative means"
             " use default settings.");
DEFINE_int32(memtable_bloom_bits, 0, "Bloom filter bits per key for memtable. "
             "Negative means no bloom filter.");

DEFINE_bool(use_existing_db, false, "If true, do not destroy the existing"
            " database.  If you set this flag and also specify a benchmark that"
            " wants a fresh database, that benchmark will fail.");

DEFINE_string(db, "", "Use the db with the following name.");

static bool ValidateCacheNumshardbits(const char* flagname, int32_t value) {
  if (value >= 20) {
    fprintf(stderr, "Invalid value for --%s: %d, must be < 20\n",
            flagname, value);
    return false;
  }
  return true;
}
DEFINE_int32(cache_numshardbits, -1, "Number of shards for the block cache"
             " is 2 ** cache_numshardbits. Negative means use default settings."
             " This is applied only if FLAGS_cache_size is non-negative.");

DEFINE_int32(cache_remove_scan_count_limit, 32, "");

DEFINE_bool(verify_checksum, false, "Verify checksum for every block read"
            " from storage");

DEFINE_bool(statistics, false, "Database statistics");
static class std::shared_ptr<rocksdb::Statistics> dbstats;

DEFINE_int64(writes, -1, "Number of write operations to do. If negative, do"
             " --num reads.");

DEFINE_int32(writes_per_second, 0, "Per-thread rate limit on writes per second."
             " No limit when <= 0. Only for the readwhilewriting test.");

DEFINE_bool(sync, false, "Sync all writes to disk");

DEFINE_bool(disable_data_sync, false, "If true, do not wait until data is"
            " synced to disk.");

DEFINE_bool(use_fsync, false, "If true, issue fsync instead of fdatasync");

DEFINE_bool(disable_wal, false, "If true, do not write WAL for write.");

DEFINE_string(wal_dir, "", "If not empty, use the given dir for WAL");

DEFINE_int32(num_levels, 7, "The total number of levels");

DEFINE_int32(target_file_size_base, 2 * 1048576, "Target file size at level-1");

DEFINE_int32(target_file_size_multiplier, 1,
             "A multiplier to compute target level-N file size (N >= 2)");

DEFINE_uint64(max_bytes_for_level_base,  10 * 1048576, "Max bytes for level-1");

DEFINE_int32(max_bytes_for_level_multiplier, 10,
             "A multiplier to compute max bytes for level-N (N >= 2)");

static std::vector<int> FLAGS_max_bytes_for_level_multiplier_additional_v;
DEFINE_string(max_bytes_for_level_multiplier_additional, "",
              "A vector that specifies additional fanout per level");

DEFINE_int32(level0_stop_writes_trigger, 12, "Number of files in level-0"
             " that will trigger put stop.");

DEFINE_int32(level0_slowdown_writes_trigger, 8, "Number of files in level-0"
             " that will slow down writes.");

DEFINE_int32(level0_file_num_compaction_trigger, 4, "Number of files in level-0"
             " when compactions start");

static bool ValidateInt32Percent(const char* flagname, int32_t value) {
  if (value <= 0 || value>=100) {
    fprintf(stderr, "Invalid value for --%s: %d, 0< pct <100 \n",
            flagname, value);
    return false;
  }
  return true;
}
DEFINE_int32(readwritepercent, 90, "Ratio of reads to reads/writes (expressed"
             " as percentage) for the ReadRandomWriteRandom workload. The "
             "default value 90 means 90% operations out of all reads and writes"
             " operations are reads. In other words, 9 gets for every 1 put.");

DEFINE_int32(mergereadpercent, 70, "Ratio of merges to merges&reads (expressed"
             " as percentage) for the ReadRandomMergeRandom workload. The"
             " default value 70 means 70% out of all read and merge operations"
             " are merges. In other words, 7 merges for every 3 gets.");

DEFINE_int32(deletepercent, 2, "Percentage of deletes out of reads/writes/"
             "deletes (used in RandomWithVerify only). RandomWithVerify "
             "calculates writepercent as (100 - FLAGS_readwritepercent - "
             "deletepercent), so deletepercent must be smaller than (100 - "
             "FLAGS_readwritepercent)");

DEFINE_uint64(delete_obsolete_files_period_micros, 0, "Option to delete "
              "obsolete files periodically. 0 means that obsolete files are"
              " deleted after every compaction run.");

namespace {
enum rocksdb::CompressionType StringToCompressionType(const char* ctype) {
  assert(ctype);

  if (!strcasecmp(ctype, "none"))
    return rocksdb::kNoCompression;
  else if (!strcasecmp(ctype, "snappy"))
    return rocksdb::kSnappyCompression;
  else if (!strcasecmp(ctype, "zlib"))
    return rocksdb::kZlibCompression;
  else if (!strcasecmp(ctype, "bzip2"))
    return rocksdb::kBZip2Compression;
  else if (!strcasecmp(ctype, "lz4"))
    return rocksdb::kLZ4Compression;
  else if (!strcasecmp(ctype, "lz4hc"))
    return rocksdb::kLZ4HCCompression;

  fprintf(stdout, "Cannot parse compression type '%s'\n", ctype);
  return rocksdb::kSnappyCompression; //default value
}
}  // namespace

DEFINE_string(compression_type, "snappy",
              "Algorithm to use to compress the database");
static enum rocksdb::CompressionType FLAGS_compression_type_e =
    rocksdb::kSnappyCompression;

DEFINE_int32(compression_level, -1,
             "Compression level. For zlib this should be -1 for the "
             "default level, or between 0 and 9.");

static bool ValidateCompressionLevel(const char* flagname, int32_t value) {
  if (value < -1 || value > 9) {
    fprintf(stderr, "Invalid value for --%s: %d, must be between -1 and 9\n",
            flagname, value);
    return false;
  }
  return true;
}

static const bool FLAGS_compression_level_dummy __attribute__((unused)) =
    RegisterFlagValidator(&FLAGS_compression_level, &ValidateCompressionLevel);

DEFINE_int32(min_level_to_compress, -1, "If non-negative, compression starts"
             " from this level. Levels with number < min_level_to_compress are"
             " not compressed. Otherwise, apply compression_type to "
             "all levels.");

static bool ValidateTableCacheNumshardbits(const char* flagname,
                                           int32_t value) {
  if (0 >= value || value > 20) {
    fprintf(stderr, "Invalid value for --%s: %d, must be  0 < val <= 20\n",
            flagname, value);
    return false;
  }
  return true;
}
DEFINE_int32(table_cache_numshardbits, 4, "");

DEFINE_string(hdfs, "", "Name of hdfs environment");
// posix or hdfs environment
static rocksdb::Env* FLAGS_env = rocksdb::Env::Default();

DEFINE_int64(stats_interval, 0, "Stats are reported every N operations when "
             "this is greater than zero. When 0 the interval grows over time.");

DEFINE_int32(stats_per_interval, 0, "Reports additional stats per interval when"
             " this is greater than 0.");

DEFINE_int32(perf_level, 0, "Level of perf collection");

static bool ValidateRateLimit(const char* flagname, double value) {
  static constexpr double EPSILON = 1e-10;
  if ( value < -EPSILON ) {
    fprintf(stderr, "Invalid value for --%s: %12.6f, must be >= 0.0\n",
            flagname, value);
    return false;
  }
  return true;
}
DEFINE_double(soft_rate_limit, 0.0, "");

DEFINE_double(hard_rate_limit, 0.0, "When not equal to 0 this make threads "
              "sleep at each stats reporting interval until the compaction"
              " score for all levels is less than or equal to this value.");

DEFINE_int32(rate_limit_delay_max_milliseconds, 1000,
             "When hard_rate_limit is set then this is the max time a put will"
             " be stalled.");

DEFINE_int32(max_grandparent_overlap_factor, 10, "Control maximum bytes of "
             "overlaps in grandparent (i.e., level+2) before we stop building a"
             " single file in a level->level+1 compaction.");

DEFINE_bool(readonly, false, "Run read only benchmarks.");

DEFINE_bool(disable_auto_compactions, false, "Do not auto trigger compactions");

DEFINE_int32(source_compaction_factor, 1, "Cap the size of data in level-K for"
             " a compaction run that compacts Level-K with Level-(K+1) (for"
             " K >= 1)");

DEFINE_uint64(wal_ttl_seconds, 0, "Set the TTL for the WAL Files in seconds.");
DEFINE_uint64(wal_size_limit_MB, 0, "Set the size limit for the WAL Files"
              " in MB.");

DEFINE_bool(bufferedio, rocksdb::EnvOptions().use_os_buffer,
            "Allow buffered io using OS buffers");

DEFINE_bool(mmap_read, rocksdb::EnvOptions().use_mmap_reads,
            "Allow reads to occur via mmap-ing files");

DEFINE_bool(mmap_write, rocksdb::EnvOptions().use_mmap_writes,
            "Allow writes to occur via mmap-ing files");

DEFINE_bool(advise_random_on_open, rocksdb::Options().advise_random_on_open,
            "Advise random access on table file open");

DEFINE_string(compaction_fadvice, "NORMAL",
              "Access pattern advice when a file is compacted");
static auto FLAGS_compaction_fadvice_e =
  rocksdb::Options().access_hint_on_compaction_start;

DEFINE_bool(use_tailing_iterator, false,
            "Use tailing iterator to access a series of keys instead of get");
DEFINE_int64(iter_refresh_interval_us, -1,
             "How often to refresh iterators. Disable refresh when -1");

DEFINE_bool(use_adaptive_mutex, rocksdb::Options().use_adaptive_mutex,
            "Use adaptive mutex");

DEFINE_uint64(bytes_per_sync,  rocksdb::Options().bytes_per_sync,
              "Allows OS to incrementally sync files to disk while they are"
              " being written, in the background. Issue one request for every"
              " bytes_per_sync written. 0 turns it off.");
DEFINE_bool(filter_deletes, false, " On true, deletes use bloom-filter and drop"
            " the delete if key not present");

DEFINE_int32(max_successive_merges, 0, "Maximum number of successive merge"
             " operations on a key in the memtable");

static bool ValidatePrefixSize(const char* flagname, int32_t value) {
  if (value < 0 || value>=2000000000) {
    fprintf(stderr, "Invalid value for --%s: %d. 0<= PrefixSize <=2000000000\n",
            flagname, value);
    return false;
  }
  return true;
}
DEFINE_int32(prefix_size, 0, "control the prefix size for HashSkipList and "
             "plain table");
DEFINE_int64(keys_per_prefix, 0, "control average number of keys generated "
             "per prefix, 0 means no special handling of the prefix, "
             "i.e. use the prefix comes with the generated random number.");
DEFINE_bool(enable_io_prio, false, "Lower the background flush/compaction "
            "threads' IO priority");

enum RepFactory {
  kSkipList,
  kPrefixHash,
  kVectorRep,
  kHashLinkedList,
  kCuckoo
};

namespace {
enum RepFactory StringToRepFactory(const char* ctype) {
  assert(ctype);

  if (!strcasecmp(ctype, "skip_list"))
    return kSkipList;
  else if (!strcasecmp(ctype, "prefix_hash"))
    return kPrefixHash;
  else if (!strcasecmp(ctype, "vector"))
    return kVectorRep;
  else if (!strcasecmp(ctype, "hash_linkedlist"))
    return kHashLinkedList;
  else if (!strcasecmp(ctype, "cuckoo"))
    return kCuckoo;

  fprintf(stdout, "Cannot parse memreptable %s\n", ctype);
  return kSkipList;
}
}  // namespace

static enum RepFactory FLAGS_rep_factory;
DEFINE_string(memtablerep, "skip_list", "");
DEFINE_int64(hash_bucket_count, 1024 * 1024, "hash bucket count");
DEFINE_bool(use_plain_table, false, "if use plain table "
            "instead of block-based table format");
DEFINE_bool(use_cuckoo_table, false, "if use cuckoo table format");
DEFINE_double(cuckoo_hash_ratio, 0.9, "Hash ratio for Cuckoo SST table.");
DEFINE_bool(use_hash_search, false, "if use kHashSearch "
            "instead of kBinarySearch. "
            "This is valid if only we use BlockTable");

DEFINE_string(merge_operator, "", "The merge operator to use with the database."
              "If a new merge operator is specified, be sure to use fresh"
              " database The possible merge operators are defined in"
              " utilities/merge_operators.h");

static const bool FLAGS_soft_rate_limit_dummy __attribute__((unused)) =
    RegisterFlagValidator(&FLAGS_soft_rate_limit, &ValidateRateLimit);

static const bool FLAGS_hard_rate_limit_dummy __attribute__((unused)) =
    RegisterFlagValidator(&FLAGS_hard_rate_limit, &ValidateRateLimit);

static const bool FLAGS_prefix_size_dummy __attribute__((unused)) =
    RegisterFlagValidator(&FLAGS_prefix_size, &ValidatePrefixSize);

static const bool FLAGS_key_size_dummy __attribute__((unused)) =
    RegisterFlagValidator(&FLAGS_key_size, &ValidateKeySize);

static const bool FLAGS_cache_numshardbits_dummy __attribute__((unused)) =
    RegisterFlagValidator(&FLAGS_cache_numshardbits,
                          &ValidateCacheNumshardbits);

static const bool FLAGS_readwritepercent_dummy __attribute__((unused)) =
    RegisterFlagValidator(&FLAGS_readwritepercent, &ValidateInt32Percent);

DEFINE_int32(disable_seek_compaction, false,
             "Not used, left here for backwards compatibility");

static const bool FLAGS_deletepercent_dummy __attribute__((unused)) =
    RegisterFlagValidator(&FLAGS_deletepercent, &ValidateInt32Percent);
static const bool FLAGS_table_cache_numshardbits_dummy __attribute__((unused)) =
    RegisterFlagValidator(&FLAGS_table_cache_numshardbits,
                          &ValidateTableCacheNumshardbits);

namespace rocksdb {

// Helper for quickly generating random data.
class RandomGenerator {
 private:
  std::string data_;
  unsigned int pos_;

 public:
  RandomGenerator() {
    // We use a limited amount of data over and over again and ensure
    // that it is larger than the compression window (32KB), and also
    // large enough to serve all typical value sizes we want to write.
    Random rnd(301);
    std::string piece;
    while (data_.size() < (unsigned)std::max(1048576, FLAGS_value_size)) {
      // Add a short fragment that is as compressible as specified
      // by FLAGS_compression_ratio.
      test::CompressibleString(&rnd, FLAGS_compression_ratio, 100, &piece);
      data_.append(piece);
    }
    pos_ = 0;
  }

  Slice Generate(unsigned int len) {
    assert(len <= data_.size());
    if (pos_ + len > data_.size()) {
      pos_ = 0;
    }
    pos_ += len;
    return Slice(data_.data() + pos_ - len, len);
  }
};

static void AppendWithSpace(std::string* str, Slice msg) {
  if (msg.empty()) return;
  if (!str->empty()) {
    str->push_back(' ');
  }
  str->append(msg.data(), msg.size());
}

class Stats {
 private:
  int id_;
  double start_;
  double finish_;
  double seconds_;
  int64_t done_;
  int64_t last_report_done_;
  int64_t next_report_;
  int64_t bytes_;
  double last_op_finish_;
  double last_report_finish_;
  HistogramImpl hist_;
  std::string message_;
  bool exclude_from_merge_;

 public:
  Stats() { Start(-1); }

  void Start(int id) {
    id_ = id;
    next_report_ = FLAGS_stats_interval ? FLAGS_stats_interval : 100;
    last_op_finish_ = start_;
    hist_.Clear();
    done_ = 0;
    last_report_done_ = 0;
    bytes_ = 0;
    seconds_ = 0;
    start_ = FLAGS_env->NowMicros();
    finish_ = start_;
    last_report_finish_ = start_;
    message_.clear();
    // When set, stats from this thread won't be merged with others.
    exclude_from_merge_ = false;
  }

  void Merge(const Stats& other) {
    if (other.exclude_from_merge_)
      return;

    hist_.Merge(other.hist_);
    done_ += other.done_;
    bytes_ += other.bytes_;
    seconds_ += other.seconds_;
    if (other.start_ < start_) start_ = other.start_;
    if (other.finish_ > finish_) finish_ = other.finish_;

    // Just keep the messages from one thread
    if (message_.empty()) message_ = other.message_;
  }

  void Stop() {
    finish_ = FLAGS_env->NowMicros();
    seconds_ = (finish_ - start_) * 1e-6;
  }

  void AddMessage(Slice msg) {
    AppendWithSpace(&message_, msg);
  }

  void SetId(int id) { id_ = id; }
  void SetExcludeFromMerge() { exclude_from_merge_ = true; }

  void FinishedOps(DB* db, int64_t num_ops) {
    if (FLAGS_histogram) {
      double now = FLAGS_env->NowMicros();
      double micros = now - last_op_finish_;
      hist_.Add(micros);
      if (micros > 20000 && !FLAGS_stats_interval) {
        fprintf(stderr, "long op: %.1f micros%30s\r", micros, "");
        fflush(stderr);
      }
      last_op_finish_ = now;
    }

    done_ += num_ops;
    if (done_ >= next_report_) {
      if (!FLAGS_stats_interval) {
        if      (next_report_ < 1000)   next_report_ += 100;
        else if (next_report_ < 5000)   next_report_ += 500;
        else if (next_report_ < 10000)  next_report_ += 1000;
        else if (next_report_ < 50000)  next_report_ += 5000;
        else if (next_report_ < 100000) next_report_ += 10000;
        else if (next_report_ < 500000) next_report_ += 50000;
        else                            next_report_ += 100000;
        fprintf(stderr, "... finished %" PRIu64 " ops%30s\r", done_, "");
        fflush(stderr);
      } else {
        double now = FLAGS_env->NowMicros();
        fprintf(stderr,
                "%s ... thread %d: (%" PRIu64 ",%" PRIu64 ") ops and "
                "(%.1f,%.1f) ops/second in (%.6f,%.6f) seconds\n",
                FLAGS_env->TimeToString((uint64_t) now/1000000).c_str(),
                id_,
                done_ - last_report_done_, done_,
                (done_ - last_report_done_) /
                ((now - last_report_finish_) / 1000000.0),
                done_ / ((now - start_) / 1000000.0),
                (now - last_report_finish_) / 1000000.0,
                (now - start_) / 1000000.0);

        if (FLAGS_stats_per_interval) {
          std::string stats;
          if (db && db->GetProperty("rocksdb.stats", &stats))
            fprintf(stderr, "%s\n", stats.c_str());
        }

        fflush(stderr);
        next_report_ += FLAGS_stats_interval;
        last_report_finish_ = now;
        last_report_done_ = done_;
      }
    }
  }

  void AddBytes(int64_t n) {
    bytes_ += n;
  }

  void Report(const Slice& name) {
    // Pretend at least one op was done in case we are running a benchmark
    // that does not call FinishedOps().
    if (done_ < 1) done_ = 1;

    std::string extra;
    if (bytes_ > 0) {
      // Rate is computed on actual elapsed time, not the sum of per-thread
      // elapsed times.
      double elapsed = (finish_ - start_) * 1e-6;
      char rate[100];
      snprintf(rate, sizeof(rate), "%6.1f MB/s",
               (bytes_ / 1048576.0) / elapsed);
      extra = rate;
    }
    AppendWithSpace(&extra, message_);
    double elapsed = (finish_ - start_) * 1e-6;
    double throughput = (double)done_/elapsed;

    fprintf(stdout, "%-12s : %11.3f micros/op %ld ops/sec;%s%s\n",
            name.ToString().c_str(),
            elapsed * 1e6 / done_,
            (long)throughput,
            (extra.empty() ? "" : " "),
            extra.c_str());
    if (FLAGS_histogram) {
      fprintf(stdout, "Microseconds per op:\n%s\n", hist_.ToString().c_str());
    }
    fflush(stdout);
  }
};

// State shared by all concurrent executions of the same benchmark.
struct SharedState {
  port::Mutex mu;
  port::CondVar cv;
  int total;
  int perf_level;

  // Each thread goes through the following states:
  //    (1) initializing
  //    (2) waiting for others to be initialized
  //    (3) running
  //    (4) done

  long num_initialized;
  long num_done;
  bool start;

  SharedState() : cv(&mu), perf_level(FLAGS_perf_level) { }
};

// Per-thread state for concurrent executions of the same benchmark.
struct ThreadState {
  int tid;             // 0..n-1 when running in n threads
  Random64 rand;         // Has different seeds for different threads
  Stats stats;
  SharedState* shared;

  /* implicit */ ThreadState(int index)
      : tid(index),
        rand((FLAGS_seed ? FLAGS_seed : 1000) + index) {
  }
};

class Duration {
 public:
  Duration(int max_seconds, int64_t max_ops) {
    max_seconds_ = max_seconds;
    max_ops_= max_ops;
    ops_ = 0;
    start_at_ = FLAGS_env->NowMicros();
  }

  bool Done(int64_t increment) {
    if (increment <= 0) increment = 1;    // avoid Done(0) and infinite loops
    ops_ += increment;

    if (max_seconds_) {
      // Recheck every appx 1000 ops (exact iff increment is factor of 1000)
      if ((ops_/1000) != ((ops_-increment)/1000)) {
        double now = FLAGS_env->NowMicros();
        return ((now - start_at_) / 1000000.0) >= max_seconds_;
      } else {
        return false;
      }
    } else {
      return ops_ > max_ops_;
    }
  }

 private:
  int max_seconds_;
  int64_t max_ops_;
  int64_t ops_;
  double start_at_;
};

class Benchmark {
 private:
  std::shared_ptr<Cache> cache_;
  std::shared_ptr<Cache> compressed_cache_;
  std::shared_ptr<const FilterPolicy> filter_policy_;
  const SliceTransform* prefix_extractor_;
  struct DBWithColumnFamilies {
    std::vector<ColumnFamilyHandle*> cfh;
    DB* db;
    DBWithColumnFamilies() : db(nullptr) {
      cfh.clear();
    }
  };
  DBWithColumnFamilies db_;
  std::vector<DBWithColumnFamilies> multi_dbs_;
  int64_t num_;
  int value_size_;
  int key_size_;
  int prefix_size_;
  int64_t keys_per_prefix_;
  int64_t entries_per_batch_;
  WriteOptions write_options_;
  int64_t reads_;
  int64_t writes_;
  int64_t readwrites_;
  int64_t merge_keys_;

  bool SanityCheck() {
    if (FLAGS_compression_ratio > 1) {
      fprintf(stderr, "compression_ratio should be between 0 and 1\n");
      return false;
    }
    return true;
  }

  void PrintHeader() {
    PrintEnvironment();
    fprintf(stdout, "Keys:       %d bytes each\n", FLAGS_key_size);
    fprintf(stdout, "Values:     %d bytes each (%d bytes after compression)\n",
            FLAGS_value_size,
            static_cast<int>(FLAGS_value_size * FLAGS_compression_ratio + 0.5));
    fprintf(stdout, "Entries:    %" PRIu64 "\n", num_);
    fprintf(stdout, "Prefix:    %d bytes\n", FLAGS_prefix_size);
    fprintf(stdout, "Keys per prefix:    %" PRIu64 "\n", keys_per_prefix_);
    fprintf(stdout, "RawSize:    %.1f MB (estimated)\n",
            ((static_cast<int64_t>(FLAGS_key_size + FLAGS_value_size) * num_)
             / 1048576.0));
    fprintf(stdout, "FileSize:   %.1f MB (estimated)\n",
            (((FLAGS_key_size + FLAGS_value_size * FLAGS_compression_ratio)
              * num_)
             / 1048576.0));
    fprintf(stdout, "Write rate limit: %d\n", FLAGS_writes_per_second);
    if (FLAGS_enable_numa) {
      fprintf(stderr, "Running in NUMA enabled mode.\n");
#ifndef NUMA
      fprintf(stderr, "NUMA is not defined in the system.\n");
      exit(1);
#else
      if (numa_available() == -1) {
        fprintf(stderr, "NUMA is not supported by the system.\n");
        exit(1);
      }
#endif
    }
    switch (FLAGS_compression_type_e) {
      case rocksdb::kNoCompression:
        fprintf(stdout, "Compression: none\n");
        break;
      case rocksdb::kSnappyCompression:
        fprintf(stdout, "Compression: snappy\n");
        break;
      case rocksdb::kZlibCompression:
        fprintf(stdout, "Compression: zlib\n");
        break;
      case rocksdb::kBZip2Compression:
        fprintf(stdout, "Compression: bzip2\n");
        break;
      case rocksdb::kLZ4Compression:
        fprintf(stdout, "Compression: lz4\n");
        break;
      case rocksdb::kLZ4HCCompression:
        fprintf(stdout, "Compression: lz4hc\n");
        break;
    }

    switch (FLAGS_rep_factory) {
      case kPrefixHash:
        fprintf(stdout, "Memtablerep: prefix_hash\n");
        break;
      case kSkipList:
        fprintf(stdout, "Memtablerep: skip_list\n");
        break;
      case kVectorRep:
        fprintf(stdout, "Memtablerep: vector\n");
        break;
      case kHashLinkedList:
        fprintf(stdout, "Memtablerep: hash_linkedlist\n");
        break;
      case kCuckoo:
        fprintf(stdout, "Memtablerep: cuckoo\n");
        break;
    }
    fprintf(stdout, "Perf Level: %d\n", FLAGS_perf_level);

    PrintWarnings();
    fprintf(stdout, "------------------------------------------------\n");
  }

  void PrintWarnings() {
#if defined(__GNUC__) && !defined(__OPTIMIZE__)
    fprintf(stdout,
            "WARNING: Optimization is disabled: benchmarks unnecessarily slow\n"
            );
#endif
#ifndef NDEBUG
    fprintf(stdout,
            "WARNING: Assertions are enabled; benchmarks unnecessarily slow\n");
#endif
    if (FLAGS_compression_type_e != rocksdb::kNoCompression) {
      // The test string should not be too small.
      const int len = FLAGS_block_size;
      char* text = (char*) malloc(len+1);
      bool result = true;
      const char* name = nullptr;
      std::string compressed;

      memset(text, (int) 'y', len);
      text[len] = '\0';
      switch (FLAGS_compression_type_e) {
        case kSnappyCompression:
          result = port::Snappy_Compress(Options().compression_opts, text,
                                         strlen(text), &compressed);
          name = "Snappy";
          break;
        case kZlibCompression:
          result = port::Zlib_Compress(Options().compression_opts, text,
                                       strlen(text), &compressed);
          name = "Zlib";
          break;
        case kBZip2Compression:
          result = port::BZip2_Compress(Options().compression_opts, text,
                                        strlen(text), &compressed);
          name = "BZip2";
          break;
        case kLZ4Compression:
          result = port::LZ4_Compress(Options().compression_opts, text,
                                      strlen(text), &compressed);
          name = "LZ4";
          break;
        case kLZ4HCCompression:
          result = port::LZ4HC_Compress(Options().compression_opts, text,
                                        strlen(text), &compressed);
          name = "LZ4HC";
          break;
        case kNoCompression:
          assert(false); // cannot happen
          break;
      }

      if (!result) {
        fprintf(stdout, "WARNING: %s compression is not enabled\n", name);
      } else if (name && compressed.size() >= strlen(text)) {
        fprintf(stdout, "WARNING: %s compression is not effective\n", name);
      }

      free(text);
    }
  }

// Current the following isn't equivalent to OS_LINUX.
#if defined(__linux)
  static Slice TrimSpace(Slice s) {
    unsigned int start = 0;
    while (start < s.size() && isspace(s[start])) {
      start++;
    }
    unsigned int limit = s.size();
    while (limit > start && isspace(s[limit-1])) {
      limit--;
    }
    return Slice(s.data() + start, limit - start);
  }
#endif

  void PrintEnvironment() {
    fprintf(stderr, "LevelDB:    version %d.%d\n",
            kMajorVersion, kMinorVersion);

#if defined(__linux)
    time_t now = time(nullptr);
    fprintf(stderr, "Date:       %s", ctime(&now));  // ctime() adds newline

    FILE* cpuinfo = fopen("/proc/cpuinfo", "r");
    if (cpuinfo != nullptr) {
      char line[1000];
      int num_cpus = 0;
      std::string cpu_type;
      std::string cache_size;
      while (fgets(line, sizeof(line), cpuinfo) != nullptr) {
        const char* sep = strchr(line, ':');
        if (sep == nullptr) {
          continue;
        }
        Slice key = TrimSpace(Slice(line, sep - 1 - line));
        Slice val = TrimSpace(Slice(sep + 1));
        if (key == "model name") {
          ++num_cpus;
          cpu_type = val.ToString();
        } else if (key == "cache size") {
          cache_size = val.ToString();
        }
      }
      fclose(cpuinfo);
      fprintf(stderr, "CPU:        %d * %s\n", num_cpus, cpu_type.c_str());
      fprintf(stderr, "CPUCache:   %s\n", cache_size.c_str());
    }
#endif
  }

 public:
  Benchmark()
  : cache_(FLAGS_cache_size >= 0 ?
           (FLAGS_cache_numshardbits >= 1 ?
            NewLRUCache(FLAGS_cache_size, FLAGS_cache_numshardbits,
                        FLAGS_cache_remove_scan_count_limit) :
            NewLRUCache(FLAGS_cache_size)) : nullptr),
    compressed_cache_(FLAGS_compressed_cache_size >= 0 ?
           (FLAGS_cache_numshardbits >= 1 ?
            NewLRUCache(FLAGS_compressed_cache_size, FLAGS_cache_numshardbits) :
            NewLRUCache(FLAGS_compressed_cache_size)) : nullptr),
    filter_policy_(FLAGS_bloom_bits >= 0
                   ? NewBloomFilterPolicy(FLAGS_bloom_bits)
                   : nullptr),
    prefix_extractor_(NewFixedPrefixTransform(FLAGS_prefix_size)),
    num_(FLAGS_num),
    value_size_(FLAGS_value_size),
    key_size_(FLAGS_key_size),
    prefix_size_(FLAGS_prefix_size),
    keys_per_prefix_(FLAGS_keys_per_prefix),
    entries_per_batch_(1),
    reads_(FLAGS_reads < 0 ? FLAGS_num : FLAGS_reads),
    writes_(FLAGS_writes < 0 ? FLAGS_num : FLAGS_writes),
    readwrites_((FLAGS_writes < 0  && FLAGS_reads < 0)? FLAGS_num :
                ((FLAGS_writes > FLAGS_reads) ? FLAGS_writes : FLAGS_reads)
               ),
    merge_keys_(FLAGS_merge_keys < 0 ? FLAGS_num : FLAGS_merge_keys) {
    if (FLAGS_prefix_size > FLAGS_key_size) {
      fprintf(stderr, "prefix size is larger than key size");
      exit(1);
    }

    std::vector<std::string> files;
    FLAGS_env->GetChildren(FLAGS_db, &files);
    for (unsigned int i = 0; i < files.size(); i++) {
      if (Slice(files[i]).starts_with("heap-")) {
        FLAGS_env->DeleteFile(FLAGS_db + "/" + files[i]);
      }
    }
    if (!FLAGS_use_existing_db) {
      DestroyDB(FLAGS_db, Options());
    }
  }

  ~Benchmark() {
    delete db_.db;
    delete prefix_extractor_;
  }

  Slice AllocateKey() {
    return Slice(new char[key_size_], key_size_);
  }

  // Generate key according to the given specification and random number.
  // The resulting key will have the following format (if keys_per_prefix_
  // is positive), extra trailing bytes are either cut off or paddd with '0'.
  // The prefix value is derived from key value.
  //   ----------------------------
  //   | prefix 00000 | key 00000 |
  //   ----------------------------
  // If keys_per_prefix_ is 0, the key is simply a binary representation of
  // random number followed by trailing '0's
  //   ----------------------------
  //   |        key 00000         |
  //   ----------------------------
  void GenerateKeyFromInt(uint64_t v, int64_t num_keys, Slice* key) {
    char* start = const_cast<char*>(key->data());
    char* pos = start;
    if (keys_per_prefix_ > 0) {
      int64_t num_prefix = num_keys / keys_per_prefix_;
      int64_t prefix = v % num_prefix;
      int bytes_to_fill = std::min(prefix_size_, 8);
      if (port::kLittleEndian) {
        for (int i = 0; i < bytes_to_fill; ++i) {
          pos[i] = (prefix >> ((bytes_to_fill - i - 1) << 3)) & 0xFF;
        }
      } else {
        memcpy(pos, static_cast<void*>(&prefix), bytes_to_fill);
      }
      if (prefix_size_ > 8) {
        // fill the rest with 0s
        memset(pos + 8, '0', prefix_size_ - 8);
      }
      pos += prefix_size_;
    }

    int bytes_to_fill = std::min(key_size_ - static_cast<int>(pos - start), 8);
    if (port::kLittleEndian) {
      for (int i = 0; i < bytes_to_fill; ++i) {
        pos[i] = (v >> ((bytes_to_fill - i - 1) << 3)) & 0xFF;
      }
    } else {
      memcpy(pos, static_cast<void*>(&v), bytes_to_fill);
    }
    pos += bytes_to_fill;
    if (key_size_ > pos - start) {
      memset(pos, '0', key_size_ - (pos - start));
    }
  }

  std::string GetDbNameForMultiple(std::string base_name, size_t id) {
    return base_name + std::to_string(id);
  }

  std::string ColumnFamilyName(int i) {
    if (i == 0) {
      return kDefaultColumnFamilyName;
    } else {
      char name[100];
      snprintf(name, sizeof(name), "column_family_name_%06d", i);
      return std::string(name);
    }
  }

  void Run() {
    if (!SanityCheck()) {
      exit(1);
    }
    PrintHeader();
    Open();
    const char* benchmarks = FLAGS_benchmarks.c_str();
    while (benchmarks != nullptr) {
      const char* sep = strchr(benchmarks, ',');
      Slice name;
      if (sep == nullptr) {
        name = benchmarks;
        benchmarks = nullptr;
      } else {
        name = Slice(benchmarks, sep - benchmarks);
        benchmarks = sep + 1;
      }

      // Sanitize parameters
      num_ = FLAGS_num;
      reads_ = (FLAGS_reads < 0 ? FLAGS_num : FLAGS_reads);
      writes_ = (FLAGS_writes < 0 ? FLAGS_num : FLAGS_writes);
      value_size_ = FLAGS_value_size;
      key_size_ = FLAGS_key_size;
      entries_per_batch_ = 1;
      write_options_ = WriteOptions();
      if (FLAGS_sync) {
        write_options_.sync = true;
      }
      write_options_.disableWAL = FLAGS_disable_wal;

      void (Benchmark::*method)(ThreadState*) = nullptr;
      bool fresh_db = false;
      int num_threads = FLAGS_threads;

      if (name == Slice("fillseq")) {
        fresh_db = true;
        method = &Benchmark::WriteSeq;
      } else if (name == Slice("fillbatch")) {
        fresh_db = true;
        entries_per_batch_ = 1000;
        method = &Benchmark::WriteSeq;
      } else if (name == Slice("fillrandom")) {
        fresh_db = true;
        method = &Benchmark::WriteRandom;
      } else if (name == Slice("filluniquerandom")) {
        fresh_db = true;
        if (num_threads > 1) {
          fprintf(stderr, "filluniquerandom multithreaded not supported"
                           ", use 1 thread");
          num_threads = 1;
        }
        method = &Benchmark::WriteUniqueRandom;
      } else if (name == Slice("overwrite")) {
        fresh_db = false;
        method = &Benchmark::WriteRandom;
      } else if (name == Slice("fillsync")) {
        fresh_db = true;
        num_ /= 1000;
        write_options_.sync = true;
        method = &Benchmark::WriteRandom;
      } else if (name == Slice("fill100K")) {
        fresh_db = true;
        num_ /= 1000;
        value_size_ = 100 * 1000;
        method = &Benchmark::WriteRandom;
      } else if (name == Slice("readseq")) {
        method = &Benchmark::ReadSequential;
      } else if (name == Slice("readtocache")) {
        method = &Benchmark::ReadSequential;
        num_threads = 1;
        reads_ = num_;
      } else if (name == Slice("readreverse")) {
        method = &Benchmark::ReadReverse;
      } else if (name == Slice("readrandom")) {
        method = &Benchmark::ReadRandom;
      } else if (name == Slice("multireadrandom")) {
        method = &Benchmark::MultiReadRandom;
      } else if (name == Slice("readmissing")) {
        ++key_size_;
        method = &Benchmark::ReadRandom;
      } else if (name == Slice("newiterator")) {
        method = &Benchmark::IteratorCreation;
      } else if (name == Slice("newiteratorwhilewriting")) {
        num_threads++;  // Add extra thread for writing
        method = &Benchmark::IteratorCreationWhileWriting;
      } else if (name == Slice("seekrandom")) {
        method = &Benchmark::SeekRandom;
      } else if (name == Slice("seekrandomwhilewriting")) {
        num_threads++;  // Add extra thread for writing
        method = &Benchmark::SeekRandomWhileWriting;
      } else if (name == Slice("readrandomsmall")) {
        reads_ /= 1000;
        method = &Benchmark::ReadRandom;
      } else if (name == Slice("deleteseq")) {
        method = &Benchmark::DeleteSeq;
      } else if (name == Slice("deleterandom")) {
        method = &Benchmark::DeleteRandom;
      } else if (name == Slice("readwhilewriting")) {
        num_threads++;  // Add extra thread for writing
        method = &Benchmark::ReadWhileWriting;
      } else if (name == Slice("readrandomwriterandom")) {
        method = &Benchmark::ReadRandomWriteRandom;
      } else if (name == Slice("readrandommergerandom")) {
        if (FLAGS_merge_operator.empty()) {
          fprintf(stdout, "%-12s : skipped (--merge_operator is unknown)\n",
                  name.ToString().c_str());
          exit(1);
        }
        method = &Benchmark::ReadRandomMergeRandom;
      } else if (name == Slice("updaterandom")) {
        method = &Benchmark::UpdateRandom;
      } else if (name == Slice("appendrandom")) {
        method = &Benchmark::AppendRandom;
      } else if (name == Slice("mergerandom")) {
        if (FLAGS_merge_operator.empty()) {
          fprintf(stdout, "%-12s : skipped (--merge_operator is unknown)\n",
                  name.ToString().c_str());
          exit(1);
        }
        method = &Benchmark::MergeRandom;
      } else if (name == Slice("randomwithverify")) {
        method = &Benchmark::RandomWithVerify;
      } else if (name == Slice("compact")) {
        method = &Benchmark::Compact;
      } else if (name == Slice("crc32c")) {
        method = &Benchmark::Crc32c;
      } else if (name == Slice("xxhash")) {
        method = &Benchmark::xxHash;
      } else if (name == Slice("acquireload")) {
        method = &Benchmark::AcquireLoad;
      } else if (name == Slice("compress")) {
        method = &Benchmark::Compress;
      } else if (name == Slice("uncompress")) {
        method = &Benchmark::Uncompress;
      } else if (name == Slice("stats")) {
        PrintStats("rocksdb.stats");
      } else if (name == Slice("levelstats")) {
        PrintStats("rocksdb.levelstats");
      } else if (name == Slice("sstables")) {
        PrintStats("rocksdb.sstables");
      } else {
        if (name != Slice()) {  // No error message for empty name
          fprintf(stderr, "unknown benchmark '%s'\n", name.ToString().c_str());
          exit(1);
        }
      }

      if (fresh_db) {
        if (FLAGS_use_existing_db) {
          fprintf(stdout, "%-12s : skipped (--use_existing_db is true)\n",
                  name.ToString().c_str());
          method = nullptr;
        } else {
          if (db_.db != nullptr) {
            delete db_.db;
            db_.db = nullptr;
            db_.cfh.clear();
            DestroyDB(FLAGS_db, Options());
          }
          for (size_t i = 0; i < multi_dbs_.size(); i++) {
            delete multi_dbs_[i].db;
            DestroyDB(GetDbNameForMultiple(FLAGS_db, i), Options());
          }
          multi_dbs_.clear();
        }
        Open();
      }

      if (method != nullptr) {
        fprintf(stdout, "DB path: [%s]\n", FLAGS_db.c_str());
        RunBenchmark(num_threads, name, method);
      }
    }
    if (FLAGS_statistics) {
     fprintf(stdout, "STATISTICS:\n%s\n", dbstats->ToString().c_str());
    }
  }

 private:
  struct ThreadArg {
    Benchmark* bm;
    SharedState* shared;
    ThreadState* thread;
    void (Benchmark::*method)(ThreadState*);
  };

  static void ThreadBody(void* v) {
    ThreadArg* arg = reinterpret_cast<ThreadArg*>(v);
    SharedState* shared = arg->shared;
    ThreadState* thread = arg->thread;
    {
      MutexLock l(&shared->mu);
      shared->num_initialized++;
      if (shared->num_initialized >= shared->total) {
        shared->cv.SignalAll();
      }
      while (!shared->start) {
        shared->cv.Wait();
      }
    }

    SetPerfLevel(static_cast<PerfLevel> (shared->perf_level));
    thread->stats.Start(thread->tid);
    (arg->bm->*(arg->method))(thread);
    thread->stats.Stop();

    {
      MutexLock l(&shared->mu);
      shared->num_done++;
      if (shared->num_done >= shared->total) {
        shared->cv.SignalAll();
      }
    }
  }

  void RunBenchmark(int n, Slice name,
                    void (Benchmark::*method)(ThreadState*)) {
    SharedState shared;
    shared.total = n;
    shared.num_initialized = 0;
    shared.num_done = 0;
    shared.start = false;

    ThreadArg* arg = new ThreadArg[n];

    for (int i = 0; i < n; i++) {
#ifdef NUMA
      if (FLAGS_enable_numa) {
        // Performs a local allocation of memory to threads in numa node.
        int n_nodes = numa_num_task_nodes();  // Number of nodes in NUMA.
        numa_exit_on_error = 1;
        int numa_node = i % n_nodes;
        bitmask* nodes = numa_allocate_nodemask();
        numa_bitmask_clearall(nodes);
        numa_bitmask_setbit(nodes, numa_node);
        // numa_bind() call binds the process to the node and these
        // properties are passed on to the thread that is created in
        // StartThread method called later in the loop.
        numa_bind(nodes);
        numa_set_strict(1);
        numa_free_nodemask(nodes);
      }
#endif
      arg[i].bm = this;
      arg[i].method = method;
      arg[i].shared = &shared;
      arg[i].thread = new ThreadState(i);
      arg[i].thread->shared = &shared;
      FLAGS_env->StartThread(ThreadBody, &arg[i]);
    }

    shared.mu.Lock();
    while (shared.num_initialized < n) {
      shared.cv.Wait();
    }

    shared.start = true;
    shared.cv.SignalAll();
    while (shared.num_done < n) {
      shared.cv.Wait();
    }
    shared.mu.Unlock();

    // Stats for some threads can be excluded.
    Stats merge_stats;
    for (int i = 0; i < n; i++) {
      merge_stats.Merge(arg[i].thread->stats);
    }
    merge_stats.Report(name);

    for (int i = 0; i < n; i++) {
      delete arg[i].thread;
    }
    delete[] arg;
  }

  void Crc32c(ThreadState* thread) {
    // Checksum about 500MB of data total
    const int size = 4096;
    const char* label = "(4K per op)";
    std::string data(size, 'x');
    int64_t bytes = 0;
    uint32_t crc = 0;
    while (bytes < 500 * 1048576) {
      crc = crc32c::Value(data.data(), size);
      thread->stats.FinishedOps(nullptr, 1);
      bytes += size;
    }
    // Print so result is not dead
    fprintf(stderr, "... crc=0x%x\r", static_cast<unsigned int>(crc));

    thread->stats.AddBytes(bytes);
    thread->stats.AddMessage(label);
  }

  void xxHash(ThreadState* thread) {
    // Checksum about 500MB of data total
    const int size = 4096;
    const char* label = "(4K per op)";
    std::string data(size, 'x');
    int64_t bytes = 0;
    unsigned int xxh32 = 0;
    while (bytes < 500 * 1048576) {
      xxh32 = XXH32(data.data(), size, 0);
      thread->stats.FinishedOps(nullptr, 1);
      bytes += size;
    }
    // Print so result is not dead
    fprintf(stderr, "... xxh32=0x%x\r", static_cast<unsigned int>(xxh32));

    thread->stats.AddBytes(bytes);
    thread->stats.AddMessage(label);
  }

  void AcquireLoad(ThreadState* thread) {
    int dummy;
    port::AtomicPointer ap(&dummy);
    int count = 0;
    void *ptr = nullptr;
    thread->stats.AddMessage("(each op is 1000 loads)");
    while (count < 100000) {
      for (int i = 0; i < 1000; i++) {
        ptr = ap.Acquire_Load();
      }
      count++;
      thread->stats.FinishedOps(nullptr, 1);
    }
    if (ptr == nullptr) exit(1); // Disable unused variable warning.
  }

  void Compress(ThreadState *thread) {
    RandomGenerator gen;
    Slice input = gen.Generate(FLAGS_block_size);
    int64_t bytes = 0;
    int64_t produced = 0;
    bool ok = true;
    std::string compressed;

    // Compress 1G
    while (ok && bytes < int64_t(1) << 30) {
      switch (FLAGS_compression_type_e) {
      case rocksdb::kSnappyCompression:
        ok = port::Snappy_Compress(Options().compression_opts, input.data(),
                                   input.size(), &compressed);
        break;
      case rocksdb::kZlibCompression:
        ok = port::Zlib_Compress(Options().compression_opts, input.data(),
                                 input.size(), &compressed);
        break;
      case rocksdb::kBZip2Compression:
        ok = port::BZip2_Compress(Options().compression_opts, input.data(),
                                  input.size(), &compressed);
        break;
      case rocksdb::kLZ4Compression:
        ok = port::LZ4_Compress(Options().compression_opts, input.data(),
                                input.size(), &compressed);
        break;
      case rocksdb::kLZ4HCCompression:
        ok = port::LZ4HC_Compress(Options().compression_opts, input.data(),
                                  input.size(), &compressed);
        break;
      default:
        ok = false;
      }
      produced += compressed.size();
      bytes += input.size();
      thread->stats.FinishedOps(nullptr, 1);
    }

    if (!ok) {
      thread->stats.AddMessage("(compression failure)");
    } else {
      char buf[100];
      snprintf(buf, sizeof(buf), "(output: %.1f%%)",
               (produced * 100.0) / bytes);
      thread->stats.AddMessage(buf);
      thread->stats.AddBytes(bytes);
    }
  }

  void Uncompress(ThreadState *thread) {
    RandomGenerator gen;
    Slice input = gen.Generate(FLAGS_block_size);
    std::string compressed;

    bool ok;
    switch (FLAGS_compression_type_e) {
    case rocksdb::kSnappyCompression:
      ok = port::Snappy_Compress(Options().compression_opts, input.data(),
                                 input.size(), &compressed);
      break;
    case rocksdb::kZlibCompression:
      ok = port::Zlib_Compress(Options().compression_opts, input.data(),
                               input.size(), &compressed);
      break;
    case rocksdb::kBZip2Compression:
      ok = port::BZip2_Compress(Options().compression_opts, input.data(),
                                input.size(), &compressed);
      break;
    case rocksdb::kLZ4Compression:
      ok = port::LZ4_Compress(Options().compression_opts, input.data(),
                              input.size(), &compressed);
      break;
    case rocksdb::kLZ4HCCompression:
      ok = port::LZ4HC_Compress(Options().compression_opts, input.data(),
                                input.size(), &compressed);
      break;
    default:
      ok = false;
    }

    int64_t bytes = 0;
    int decompress_size;
    while (ok && bytes < 1024 * 1048576) {
      char *uncompressed = nullptr;
      switch (FLAGS_compression_type_e) {
      case rocksdb::kSnappyCompression:
        // allocate here to make comparison fair
        uncompressed = new char[input.size()];
        ok = port::Snappy_Uncompress(compressed.data(), compressed.size(),
                                     uncompressed);
        break;
      case rocksdb::kZlibCompression:
        uncompressed = port::Zlib_Uncompress(
            compressed.data(), compressed.size(), &decompress_size);
        ok = uncompressed != nullptr;
        break;
      case rocksdb::kBZip2Compression:
        uncompressed = port::BZip2_Uncompress(
            compressed.data(), compressed.size(), &decompress_size);
        ok = uncompressed != nullptr;
        break;
      case rocksdb::kLZ4Compression:
        uncompressed = port::LZ4_Uncompress(
            compressed.data(), compressed.size(), &decompress_size);
        ok = uncompressed != nullptr;
        break;
      case rocksdb::kLZ4HCCompression:
        uncompressed = port::LZ4_Uncompress(
            compressed.data(), compressed.size(), &decompress_size);
        ok = uncompressed != nullptr;
        break;
      default:
        ok = false;
      }
      delete[] uncompressed;
      bytes += input.size();
      thread->stats.FinishedOps(nullptr, 1);
    }

    if (!ok) {
      thread->stats.AddMessage("(compression failure)");
    } else {
      thread->stats.AddBytes(bytes);
    }
  }

  void Open() {
    assert(db_.db == nullptr);
    Options options;
    options.create_if_missing = !FLAGS_use_existing_db;
    options.create_missing_column_families = FLAGS_num_column_families > 1;
    options.write_buffer_size = FLAGS_write_buffer_size;
    options.max_write_buffer_number = FLAGS_max_write_buffer_number;
    options.min_write_buffer_number_to_merge =
      FLAGS_min_write_buffer_number_to_merge;
    options.max_background_compactions = FLAGS_max_background_compactions;
    options.max_background_flushes = FLAGS_max_background_flushes;
    options.compaction_style = FLAGS_compaction_style_e;
    if (FLAGS_prefix_size != 0) {
      options.prefix_extractor.reset(
          NewFixedPrefixTransform(FLAGS_prefix_size));
    }
    if (FLAGS_use_uint64_comparator) {
      options.comparator = test::Uint64Comparator();
      if (FLAGS_key_size != 8) {
        fprintf(stderr, "Using Uint64 comparator but key size is not 8.\n");
        exit(1);
      }
    }
    options.memtable_prefix_bloom_bits = FLAGS_memtable_bloom_bits;
    options.bloom_locality = FLAGS_bloom_locality;
    options.max_open_files = FLAGS_open_files;
    options.statistics = dbstats;
    if (FLAGS_enable_io_prio) {
      FLAGS_env->LowerThreadPoolIOPriority(Env::LOW);
      FLAGS_env->LowerThreadPoolIOPriority(Env::HIGH);
    }
    options.env = FLAGS_env;
    options.disableDataSync = FLAGS_disable_data_sync;
    options.use_fsync = FLAGS_use_fsync;
    options.wal_dir = FLAGS_wal_dir;
    options.num_levels = FLAGS_num_levels;
    options.target_file_size_base = FLAGS_target_file_size_base;
    options.target_file_size_multiplier = FLAGS_target_file_size_multiplier;
    options.max_bytes_for_level_base = FLAGS_max_bytes_for_level_base;
    options.max_bytes_for_level_multiplier =
        FLAGS_max_bytes_for_level_multiplier;
    options.filter_deletes = FLAGS_filter_deletes;
    if ((FLAGS_prefix_size == 0) && (FLAGS_rep_factory == kPrefixHash ||
                                     FLAGS_rep_factory == kHashLinkedList)) {
      fprintf(stderr, "prefix_size should be non-zero if PrefixHash or "
                      "HashLinkedList memtablerep is used\n");
      exit(1);
    }
    switch (FLAGS_rep_factory) {
      case kPrefixHash:
        options.memtable_factory.reset(NewHashSkipListRepFactory(
            FLAGS_hash_bucket_count));
        break;
      case kSkipList:
        // no need to do anything
        break;
      case kHashLinkedList:
        options.memtable_factory.reset(NewHashLinkListRepFactory(
            FLAGS_hash_bucket_count));
        break;
      case kVectorRep:
        options.memtable_factory.reset(
          new VectorRepFactory
        );
        break;
      case kCuckoo:
        options.memtable_factory.reset(NewHashCuckooRepFactory(
            options.write_buffer_size, FLAGS_key_size + FLAGS_value_size));
        break;
    }
    if (FLAGS_use_plain_table) {
      if (FLAGS_rep_factory != kPrefixHash &&
          FLAGS_rep_factory != kHashLinkedList) {
        fprintf(stderr, "Waring: plain table is used with skipList\n");
      }
      if (!FLAGS_mmap_read && !FLAGS_mmap_write) {
        fprintf(stderr, "plain table format requires mmap to operate\n");
        exit(1);
      }

      int bloom_bits_per_key = FLAGS_bloom_bits;
      if (bloom_bits_per_key < 0) {
        bloom_bits_per_key = 0;
      }

      PlainTableOptions plain_table_options;
      plain_table_options.user_key_len = FLAGS_key_size;
      plain_table_options.bloom_bits_per_key = bloom_bits_per_key;
      plain_table_options.hash_table_ratio = 0.75;
      options.table_factory = std::shared_ptr<TableFactory>(
          NewPlainTableFactory(plain_table_options));
    } else if (FLAGS_use_cuckoo_table) {
      if (FLAGS_cuckoo_hash_ratio > 1 || FLAGS_cuckoo_hash_ratio < 0) {
        fprintf(stderr, "Invalid cuckoo_hash_ratio\n");
        exit(1);
      }
      options.table_factory = std::shared_ptr<TableFactory>(
          NewCuckooTableFactory(FLAGS_cuckoo_hash_ratio));
    } else {
      BlockBasedTableOptions block_based_options;
      if (FLAGS_use_hash_search) {
        if (FLAGS_prefix_size == 0) {
          fprintf(stderr,
              "prefix_size not assigned when enable use_hash_search \n");
          exit(1);
        }
        block_based_options.index_type = BlockBasedTableOptions::kHashSearch;
      } else {
        block_based_options.index_type = BlockBasedTableOptions::kBinarySearch;
      }
      if (cache_ == nullptr) {
        block_based_options.no_block_cache = true;
      }
      block_based_options.block_cache = cache_;
      block_based_options.block_cache_compressed = compressed_cache_;
      block_based_options.block_size = FLAGS_block_size;
      block_based_options.block_restart_interval = FLAGS_block_restart_interval;
      block_based_options.filter_policy = filter_policy_;
      options.table_factory.reset(
          NewBlockBasedTableFactory(block_based_options));
    }
    if (FLAGS_max_bytes_for_level_multiplier_additional_v.size() > 0) {
      if (FLAGS_max_bytes_for_level_multiplier_additional_v.size() !=
          (unsigned int)FLAGS_num_levels) {
        fprintf(stderr, "Insufficient number of fanouts specified %d\n",
                (int)FLAGS_max_bytes_for_level_multiplier_additional_v.size());
        exit(1);
      }
      options.max_bytes_for_level_multiplier_additional =
        FLAGS_max_bytes_for_level_multiplier_additional_v;
    }
    options.level0_stop_writes_trigger = FLAGS_level0_stop_writes_trigger;
    options.level0_file_num_compaction_trigger =
        FLAGS_level0_file_num_compaction_trigger;
    options.level0_slowdown_writes_trigger =
      FLAGS_level0_slowdown_writes_trigger;
    options.compression = FLAGS_compression_type_e;
    options.compression_opts.level = FLAGS_compression_level;
    options.WAL_ttl_seconds = FLAGS_wal_ttl_seconds;
    options.WAL_size_limit_MB = FLAGS_wal_size_limit_MB;
    if (FLAGS_min_level_to_compress >= 0) {
      assert(FLAGS_min_level_to_compress <= FLAGS_num_levels);
      options.compression_per_level.resize(FLAGS_num_levels);
      for (int i = 0; i < FLAGS_min_level_to_compress; i++) {
        options.compression_per_level[i] = kNoCompression;
      }
      for (int i = FLAGS_min_level_to_compress;
           i < FLAGS_num_levels; i++) {
        options.compression_per_level[i] = FLAGS_compression_type_e;
      }
    }
    options.delete_obsolete_files_period_micros =
      FLAGS_delete_obsolete_files_period_micros;
    options.soft_rate_limit = FLAGS_soft_rate_limit;
    options.hard_rate_limit = FLAGS_hard_rate_limit;
    options.rate_limit_delay_max_milliseconds =
      FLAGS_rate_limit_delay_max_milliseconds;
    options.table_cache_numshardbits = FLAGS_table_cache_numshardbits;
    options.max_grandparent_overlap_factor =
      FLAGS_max_grandparent_overlap_factor;
    options.disable_auto_compactions = FLAGS_disable_auto_compactions;
    options.source_compaction_factor = FLAGS_source_compaction_factor;

    // fill storage options
    options.allow_os_buffer = FLAGS_bufferedio;
    options.allow_mmap_reads = FLAGS_mmap_read;
    options.allow_mmap_writes = FLAGS_mmap_write;
    options.advise_random_on_open = FLAGS_advise_random_on_open;
    options.access_hint_on_compaction_start = FLAGS_compaction_fadvice_e;
    options.use_adaptive_mutex = FLAGS_use_adaptive_mutex;
    options.bytes_per_sync = FLAGS_bytes_per_sync;

    // merge operator options
    options.merge_operator = MergeOperators::CreateFromStringId(
        FLAGS_merge_operator);
    if (options.merge_operator == nullptr && !FLAGS_merge_operator.empty()) {
      fprintf(stderr, "invalid merge operator: %s\n",
              FLAGS_merge_operator.c_str());
      exit(1);
    }
    options.max_successive_merges = FLAGS_max_successive_merges;

    // set universal style compaction configurations, if applicable
    if (FLAGS_universal_size_ratio != 0) {
      options.compaction_options_universal.size_ratio =
        FLAGS_universal_size_ratio;
    }
    if (FLAGS_universal_min_merge_width != 0) {
      options.compaction_options_universal.min_merge_width =
        FLAGS_universal_min_merge_width;
    }
    if (FLAGS_universal_max_merge_width != 0) {
      options.compaction_options_universal.max_merge_width =
        FLAGS_universal_max_merge_width;
    }
    if (FLAGS_universal_max_size_amplification_percent != 0) {
      options.compaction_options_universal.max_size_amplification_percent =
        FLAGS_universal_max_size_amplification_percent;
    }
    if (FLAGS_universal_compression_size_percent != -1) {
      options.compaction_options_universal.compression_size_percent =
        FLAGS_universal_compression_size_percent;
    }

    if (FLAGS_num_multi_db <= 1) {
      OpenDb(options, FLAGS_db, &db_);
    } else {
      multi_dbs_.clear();
      multi_dbs_.resize(FLAGS_num_multi_db);
      for (int i = 0; i < FLAGS_num_multi_db; i++) {
        OpenDb(options, GetDbNameForMultiple(FLAGS_db, i), &multi_dbs_[i]);
      }
    }
    if (FLAGS_min_level_to_compress >= 0) {
      options.compression_per_level.clear();
    }
  }

  void OpenDb(const Options& options, const std::string& db_name,
      DBWithColumnFamilies* db) {
    Status s;
    // Open with column families if necessary.
    if (FLAGS_num_column_families > 1) {
      db->cfh.resize(FLAGS_num_column_families);
      std::vector<ColumnFamilyDescriptor> column_families;
      for (int i = 0; i < FLAGS_num_column_families; i++) {
        column_families.push_back(ColumnFamilyDescriptor(
              ColumnFamilyName(i), ColumnFamilyOptions(options)));
      }
      if (FLAGS_readonly) {
        s = DB::OpenForReadOnly(options, db_name, column_families,
            &db->cfh, &db->db);
      } else {
        s = DB::Open(options, db_name, column_families, &db->cfh, &db->db);
      }
    } else if (FLAGS_readonly) {
      s = DB::OpenForReadOnly(options, db_name, &db->db);
    } else {
      s = DB::Open(options, db_name, &db->db);
    }
    if (!s.ok()) {
      fprintf(stderr, "open error: %s\n", s.ToString().c_str());
      exit(1);
    }
  }

  enum WriteMode {
    RANDOM, SEQUENTIAL, UNIQUE_RANDOM
  };

  void WriteSeq(ThreadState* thread) {
    DoWrite(thread, SEQUENTIAL);
  }

  void WriteRandom(ThreadState* thread) {
    DoWrite(thread, RANDOM);
  }

  void WriteUniqueRandom(ThreadState* thread) {
    DoWrite(thread, UNIQUE_RANDOM);
  }

  class KeyGenerator {
   public:
    KeyGenerator(Random64* rand, WriteMode mode,
        uint64_t num, uint64_t num_per_set = 64 * 1024)
      : rand_(rand),
        mode_(mode),
        num_(num),
        next_(0) {
      if (mode_ == UNIQUE_RANDOM) {
        // NOTE: if memory consumption of this approach becomes a concern,
        // we can either break it into pieces and only random shuffle a section
        // each time. Alternatively, use a bit map implementation
        // (https://reviews.facebook.net/differential/diff/54627/)
        values_.resize(num_);
        for (uint64_t i = 0; i < num_; ++i) {
          values_[i] = i;
        }
        std::shuffle(values_.begin(), values_.end(),
            std::default_random_engine(FLAGS_seed));
      }
    }

    uint64_t Next() {
      switch (mode_) {
        case SEQUENTIAL:
          return next_++;
        case RANDOM:
          return rand_->Next() % num_;
        case UNIQUE_RANDOM:
          return values_[next_++];
      }
      assert(false);
      return std::numeric_limits<uint64_t>::max();
    }

   private:
    Random64* rand_;
    WriteMode mode_;
    const uint64_t num_;
    uint64_t next_;
    std::vector<uint64_t> values_;
  };

  DB* SelectDB(ThreadState* thread) {
    return SelectDBWithCfh(thread)->db;
  }

  DBWithColumnFamilies* SelectDBWithCfh(ThreadState* thread) {
    return SelectDBWithCfh(thread->rand.Next());
  }

  DBWithColumnFamilies* SelectDBWithCfh(uint64_t rand_int) {
    if (db_.db != nullptr) {
      return &db_;
    } else  {
      return &multi_dbs_[rand_int % multi_dbs_.size()];
    }
  }

  void DoWrite(ThreadState* thread, WriteMode write_mode) {
    const int test_duration = write_mode == RANDOM ? FLAGS_duration : 0;
    const int64_t num_ops = writes_ == 0 ? num_ : writes_;

    size_t num_key_gens = 1;
    if (db_.db == nullptr) {
      num_key_gens = multi_dbs_.size();
    }
    std::vector<std::unique_ptr<KeyGenerator>> key_gens(num_key_gens);
    Duration duration(test_duration, num_ops * num_key_gens);
    for (size_t i = 0; i < num_key_gens; i++) {
      key_gens[i].reset(new KeyGenerator(&(thread->rand), write_mode, num_ops));
    }

    if (num_ != FLAGS_num) {
      char msg[100];
      snprintf(msg, sizeof(msg), "(%" PRIu64 " ops)", num_);
      thread->stats.AddMessage(msg);
    }

    RandomGenerator gen;
    WriteBatch batch;
    Status s;
    int64_t bytes = 0;

    Slice key = AllocateKey();
    std::unique_ptr<const char[]> key_guard(key.data());
    while (!duration.Done(entries_per_batch_)) {
      size_t id = thread->rand.Next() % num_key_gens;
      DBWithColumnFamilies* db_with_cfh = SelectDBWithCfh(id);
      batch.Clear();
      for (int64_t j = 0; j < entries_per_batch_; j++) {
        int64_t rand_num = key_gens[id]->Next();
        GenerateKeyFromInt(rand_num, FLAGS_num, &key);
        if (FLAGS_num_column_families <= 1) {
          batch.Put(key, gen.Generate(value_size_));
        } else {
          // We use same rand_num as seed for key and column family so that we
          // can deterministically find the cfh corresponding to a particular
          // key while reading the key.
          batch.Put(db_with_cfh->cfh[rand_num % db_with_cfh->cfh.size()],
              key, gen.Generate(value_size_));
        }
        bytes += value_size_ + key_size_;
      }
      s = db_with_cfh->db->Write(write_options_, &batch);
      thread->stats.FinishedOps(db_with_cfh->db, entries_per_batch_);
      if (!s.ok()) {
        fprintf(stderr, "put error: %s\n", s.ToString().c_str());
        exit(1);
      }
    }
    thread->stats.AddBytes(bytes);
  }

  void ReadSequential(ThreadState* thread) {
    if (db_.db != nullptr) {
      ReadSequential(thread, db_.db);
    } else {
      for (const auto& db_with_cfh : multi_dbs_) {
        ReadSequential(thread, db_with_cfh.db);
      }
    }
  }

  void ReadSequential(ThreadState* thread, DB* db) {
    Iterator* iter = db->NewIterator(ReadOptions(FLAGS_verify_checksum, true));
    int64_t i = 0;
    int64_t bytes = 0;
    for (iter->SeekToFirst(); i < reads_ && iter->Valid(); iter->Next()) {
      bytes += iter->key().size() + iter->value().size();
      thread->stats.FinishedOps(db, 1);
      ++i;
    }
    delete iter;
    thread->stats.AddBytes(bytes);
  }

  void ReadReverse(ThreadState* thread) {
    if (db_.db != nullptr) {
      ReadReverse(thread, db_.db);
    } else {
      for (const auto& db_with_cfh : multi_dbs_) {
        ReadReverse(thread, db_with_cfh.db);
      }
    }
  }

  void ReadReverse(ThreadState* thread, DB* db) {
    Iterator* iter = db->NewIterator(ReadOptions(FLAGS_verify_checksum, true));
    int64_t i = 0;
    int64_t bytes = 0;
    for (iter->SeekToLast(); i < reads_ && iter->Valid(); iter->Prev()) {
      bytes += iter->key().size() + iter->value().size();
      thread->stats.FinishedOps(db, 1);
      ++i;
    }
    delete iter;
    thread->stats.AddBytes(bytes);
  }

  void ReadRandom(ThreadState* thread) {
    int64_t read = 0;
    int64_t found = 0;
    ReadOptions options(FLAGS_verify_checksum, true);
    Slice key = AllocateKey();
    std::unique_ptr<const char[]> key_guard(key.data());
    std::string value;

    Duration duration(FLAGS_duration, reads_);
    while (!duration.Done(1)) {
      DBWithColumnFamilies* db_with_cfh = SelectDBWithCfh(thread);
      // We use same key_rand as seed for key and column family so that we can
      // deterministically find the cfh corresponding to a particular key, as it
      // is done in DoWrite method.
      int64_t key_rand = thread->rand.Next() % FLAGS_num;
      GenerateKeyFromInt(key_rand, FLAGS_num, &key);
      read++;
      Status s;
      if (FLAGS_num_column_families > 1) {
        s = db_with_cfh->db->Get(options,
            db_with_cfh->cfh[key_rand % db_with_cfh->cfh.size()], key, &value);
      } else {
        s = db_with_cfh->db->Get(options, key, &value);
      }
      if (s.ok()) {
        found++;
      }
      thread->stats.FinishedOps(db_with_cfh->db, 1);
    }

    char msg[100];
    snprintf(msg, sizeof(msg), "(%" PRIu64 " of %" PRIu64 " found)\n",
             found, read);

    thread->stats.AddMessage(msg);

    if (FLAGS_perf_level > 0) {
      thread->stats.AddMessage(perf_context.ToString());
    }
  }

  // Calls MultiGet over a list of keys from a random distribution.
  // Returns the total number of keys found.
  void MultiReadRandom(ThreadState* thread) {
    int64_t read = 0;
    int64_t found = 0;
    ReadOptions options(FLAGS_verify_checksum, true);
    std::vector<Slice> keys;
    std::vector<std::string> values(entries_per_batch_);
    while (static_cast<int64_t>(keys.size()) < entries_per_batch_) {
      keys.push_back(AllocateKey());
    }

    Duration duration(FLAGS_duration, reads_);
    while (!duration.Done(1)) {
      DB* db = SelectDB(thread);
      for (int64_t i = 0; i < entries_per_batch_; ++i) {
        GenerateKeyFromInt(thread->rand.Next() % FLAGS_num,
            FLAGS_num, &keys[i]);
      }
      std::vector<Status> statuses = db->MultiGet(options, keys, &values);
      assert(static_cast<int64_t>(statuses.size()) == entries_per_batch_);

      read += entries_per_batch_;
      for (int64_t i = 0; i < entries_per_batch_; ++i) {
        if (statuses[i].ok()) {
          ++found;
        }
      }
      thread->stats.FinishedOps(db, entries_per_batch_);
    }
    for (auto& k : keys) {
      delete k.data();
    }

    char msg[100];
    snprintf(msg, sizeof(msg), "(%" PRIu64 " of %" PRIu64 " found)",
             found, read);
    thread->stats.AddMessage(msg);
  }

  void IteratorCreation(ThreadState* thread) {
    Duration duration(FLAGS_duration, reads_);
    ReadOptions options(FLAGS_verify_checksum, true);
    while (!duration.Done(1)) {
      DB* db = SelectDB(thread);
      Iterator* iter = db->NewIterator(options);
      delete iter;
      thread->stats.FinishedOps(db, 1);
    }
  }

  void IteratorCreationWhileWriting(ThreadState* thread) {
    if (thread->tid > 0) {
      IteratorCreation(thread);
    } else {
      BGWriter(thread);
    }
  }

  void SeekRandom(ThreadState* thread) {
    int64_t read = 0;
    int64_t found = 0;
    ReadOptions options(FLAGS_verify_checksum, true);
    options.tailing = FLAGS_use_tailing_iterator;

    Iterator* single_iter = nullptr;
    std::vector<Iterator*> multi_iters;
    if (db_.db != nullptr) {
      single_iter = db_.db->NewIterator(options);
    } else {
      for (const auto& db_with_cfh : multi_dbs_) {
        multi_iters.push_back(db_with_cfh.db->NewIterator(options));
      }
    }
    uint64_t last_refresh = FLAGS_env->NowMicros();

    Slice key = AllocateKey();
    std::unique_ptr<const char[]> key_guard(key.data());

    Duration duration(FLAGS_duration, reads_);
    while (!duration.Done(1)) {
      if (!FLAGS_use_tailing_iterator && FLAGS_iter_refresh_interval_us >= 0) {
        uint64_t now = FLAGS_env->NowMicros();
        if (now - last_refresh > (uint64_t)FLAGS_iter_refresh_interval_us) {
          if (db_.db != nullptr) {
            delete single_iter;
            single_iter = db_.db->NewIterator(options);
          } else {
            for (auto iter : multi_iters) {
              delete iter;
            }
            multi_iters.clear();
            for (const auto& db_with_cfh : multi_dbs_) {
              multi_iters.push_back(db_with_cfh.db->NewIterator(options));
            }
          }
        }
        last_refresh = now;
      }
      // Pick a Iterator to use
      Iterator* iter_to_use = single_iter;
      if (single_iter == nullptr) {
        iter_to_use = multi_iters[thread->rand.Next() % multi_iters.size()];
      }

      GenerateKeyFromInt(thread->rand.Next() % FLAGS_num, FLAGS_num, &key);
      iter_to_use->Seek(key);
      read++;
      if (iter_to_use->Valid() && iter_to_use->key().compare(key) == 0) {
        found++;
      }
      thread->stats.FinishedOps(db_.db, 1);
    }
    delete single_iter;
    for (auto iter : multi_iters) {
      delete iter;
    }

    char msg[100];
    snprintf(msg, sizeof(msg), "(%" PRIu64 " of %" PRIu64 " found)\n",
             found, read);
    thread->stats.AddMessage(msg);
    if (FLAGS_perf_level > 0) {
      thread->stats.AddMessage(perf_context.ToString());
    }
  }

  void SeekRandomWhileWriting(ThreadState* thread) {
    if (thread->tid > 0) {
      SeekRandom(thread);
    } else {
      BGWriter(thread);
    }
  }

  void DoDelete(ThreadState* thread, bool seq) {
    WriteBatch batch;
    Duration duration(seq ? 0 : FLAGS_duration, num_);
    int64_t i = 0;
    Slice key = AllocateKey();
    std::unique_ptr<const char[]> key_guard(key.data());

    while (!duration.Done(entries_per_batch_)) {
      DB* db = SelectDB(thread);
      batch.Clear();
      for (int64_t j = 0; j < entries_per_batch_; ++j) {
        const int64_t k = seq ? i + j : (thread->rand.Next() % FLAGS_num);
        GenerateKeyFromInt(k, FLAGS_num, &key);
        batch.Delete(key);
      }
      auto s = db->Write(write_options_, &batch);
      thread->stats.FinishedOps(db, entries_per_batch_);
      if (!s.ok()) {
        fprintf(stderr, "del error: %s\n", s.ToString().c_str());
        exit(1);
      }
      i += entries_per_batch_;
    }
  }

  void DeleteSeq(ThreadState* thread) {
    DoDelete(thread, true);
  }

  void DeleteRandom(ThreadState* thread) {
    DoDelete(thread, false);
  }

  void ReadWhileWriting(ThreadState* thread) {
    if (thread->tid > 0) {
      ReadRandom(thread);
    } else {
      BGWriter(thread);
    }
  }

  void BGWriter(ThreadState* thread) {
    // Special thread that keeps writing until other threads are done.
    RandomGenerator gen;
    double last = FLAGS_env->NowMicros();
    int writes_per_second_by_10 = 0;
    int num_writes = 0;

    // --writes_per_second rate limit is enforced per 100 milliseconds
    // intervals to avoid a burst of writes at the start of each second.

    if (FLAGS_writes_per_second > 0)
      writes_per_second_by_10 = FLAGS_writes_per_second / 10;

    // Don't merge stats from this thread with the readers.
    thread->stats.SetExcludeFromMerge();

    Slice key = AllocateKey();
    std::unique_ptr<const char[]> key_guard(key.data());

    while (true) {
      DB* db = SelectDB(thread);
      {
        MutexLock l(&thread->shared->mu);
        if (thread->shared->num_done + 1 >= thread->shared->num_initialized) {
          // Other threads have finished
          break;
        }
      }

      GenerateKeyFromInt(thread->rand.Next() % FLAGS_num, FLAGS_num, &key);
      Status s = db->Put(write_options_, key, gen.Generate(value_size_));
      if (!s.ok()) {
        fprintf(stderr, "put error: %s\n", s.ToString().c_str());
        exit(1);
      }
      thread->stats.FinishedOps(db_.db, 1);

      ++num_writes;
      if (writes_per_second_by_10 && num_writes >= writes_per_second_by_10) {
        double now = FLAGS_env->NowMicros();
        double usecs_since_last = now - last;

        num_writes = 0;
        last = now;

        if (usecs_since_last < 100000.0) {
          FLAGS_env->SleepForMicroseconds(100000.0 - usecs_since_last);
          last = FLAGS_env->NowMicros();
        }
      }
    }
  }

  // Given a key K and value V, this puts (K+"0", V), (K+"1", V), (K+"2", V)
  // in DB atomically i.e in a single batch. Also refer GetMany.
  Status PutMany(DB* db, const WriteOptions& writeoptions, const Slice& key,
                 const Slice& value) {
    std::string suffixes[3] = {"2", "1", "0"};
    std::string keys[3];

    WriteBatch batch;
    Status s;
    for (int i = 0; i < 3; i++) {
      keys[i] = key.ToString() + suffixes[i];
      batch.Put(keys[i], value);
    }

    s = db->Write(writeoptions, &batch);
    return s;
  }


  // Given a key K, this deletes (K+"0", V), (K+"1", V), (K+"2", V)
  // in DB atomically i.e in a single batch. Also refer GetMany.
  Status DeleteMany(DB* db, const WriteOptions& writeoptions,
                    const Slice& key) {
    std::string suffixes[3] = {"1", "2", "0"};
    std::string keys[3];

    WriteBatch batch;
    Status s;
    for (int i = 0; i < 3; i++) {
      keys[i] = key.ToString() + suffixes[i];
      batch.Delete(keys[i]);
    }

    s = db->Write(writeoptions, &batch);
    return s;
  }

  // Given a key K and value V, this gets values for K+"0", K+"1" and K+"2"
  // in the same snapshot, and verifies that all the values are identical.
  // ASSUMES that PutMany was used to put (K, V) into the DB.
  Status GetMany(DB* db, const ReadOptions& readoptions, const Slice& key,
                 std::string* value) {
    std::string suffixes[3] = {"0", "1", "2"};
    std::string keys[3];
    Slice key_slices[3];
    std::string values[3];
    ReadOptions readoptionscopy = readoptions;
    readoptionscopy.snapshot = db->GetSnapshot();
    Status s;
    for (int i = 0; i < 3; i++) {
      keys[i] = key.ToString() + suffixes[i];
      key_slices[i] = keys[i];
      s = db->Get(readoptionscopy, key_slices[i], value);
      if (!s.ok() && !s.IsNotFound()) {
        fprintf(stderr, "get error: %s\n", s.ToString().c_str());
        values[i] = "";
        // we continue after error rather than exiting so that we can
        // find more errors if any
      } else if (s.IsNotFound()) {
        values[i] = "";
      } else {
        values[i] = *value;
      }
    }
    db->ReleaseSnapshot(readoptionscopy.snapshot);

    if ((values[0] != values[1]) || (values[1] != values[2])) {
      fprintf(stderr, "inconsistent values for key %s: %s, %s, %s\n",
              key.ToString().c_str(), values[0].c_str(), values[1].c_str(),
              values[2].c_str());
      // we continue after error rather than exiting so that we can
      // find more errors if any
    }

    return s;
  }

  // Differs from readrandomwriterandom in the following ways:
  // (a) Uses GetMany/PutMany to read/write key values. Refer to those funcs.
  // (b) Does deletes as well (per FLAGS_deletepercent)
  // (c) In order to achieve high % of 'found' during lookups, and to do
  //     multiple writes (including puts and deletes) it uses upto
  //     FLAGS_numdistinct distinct keys instead of FLAGS_num distinct keys.
  // (d) Does not have a MultiGet option.
  void RandomWithVerify(ThreadState* thread) {
    ReadOptions options(FLAGS_verify_checksum, true);
    RandomGenerator gen;
    std::string value;
    int64_t found = 0;
    int get_weight = 0;
    int put_weight = 0;
    int delete_weight = 0;
    int64_t gets_done = 0;
    int64_t puts_done = 0;
    int64_t deletes_done = 0;

    Slice key = AllocateKey();
    std::unique_ptr<const char[]> key_guard(key.data());

    // the number of iterations is the larger of read_ or write_
    for (int64_t i = 0; i < readwrites_; i++) {
      DB* db = SelectDB(thread);
      if (get_weight == 0 && put_weight == 0 && delete_weight == 0) {
        // one batch completed, reinitialize for next batch
        get_weight = FLAGS_readwritepercent;
        delete_weight = FLAGS_deletepercent;
        put_weight = 100 - get_weight - delete_weight;
      }
      GenerateKeyFromInt(thread->rand.Next() % FLAGS_numdistinct,
          FLAGS_numdistinct, &key);
      if (get_weight > 0) {
        // do all the gets first
        Status s = GetMany(db, options, key, &value);
        if (!s.ok() && !s.IsNotFound()) {
          fprintf(stderr, "getmany error: %s\n", s.ToString().c_str());
          // we continue after error rather than exiting so that we can
          // find more errors if any
        } else if (!s.IsNotFound()) {
          found++;
        }
        get_weight--;
        gets_done++;
      } else if (put_weight > 0) {
        // then do all the corresponding number of puts
        // for all the gets we have done earlier
        Status s = PutMany(db, write_options_, key, gen.Generate(value_size_));
        if (!s.ok()) {
          fprintf(stderr, "putmany error: %s\n", s.ToString().c_str());
          exit(1);
        }
        put_weight--;
        puts_done++;
      } else if (delete_weight > 0) {
        Status s = DeleteMany(db, write_options_, key);
        if (!s.ok()) {
          fprintf(stderr, "deletemany error: %s\n", s.ToString().c_str());
          exit(1);
        }
        delete_weight--;
        deletes_done++;
      }

      thread->stats.FinishedOps(db_.db, 1);
    }
    char msg[100];
    snprintf(msg, sizeof(msg),
             "( get:%" PRIu64 " put:%" PRIu64 " del:%" PRIu64 " total:%" \
             PRIu64 " found:%" PRIu64 ")",
             gets_done, puts_done, deletes_done, readwrites_, found);
    thread->stats.AddMessage(msg);
  }

  // This is different from ReadWhileWriting because it does not use
  // an extra thread.
  void ReadRandomWriteRandom(ThreadState* thread) {
    ReadOptions options(FLAGS_verify_checksum, true);
    RandomGenerator gen;
    std::string value;
    int64_t found = 0;
    int get_weight = 0;
    int put_weight = 0;
    int64_t reads_done = 0;
    int64_t writes_done = 0;
    Duration duration(FLAGS_duration, readwrites_);

    Slice key = AllocateKey();
    std::unique_ptr<const char[]> key_guard(key.data());

    // the number of iterations is the larger of read_ or write_
    while (!duration.Done(1)) {
      DB* db = SelectDB(thread);
      GenerateKeyFromInt(thread->rand.Next() % FLAGS_num, FLAGS_num, &key);
      if (get_weight == 0 && put_weight == 0) {
        // one batch completed, reinitialize for next batch
        get_weight = FLAGS_readwritepercent;
        put_weight = 100 - get_weight;
      }
      if (get_weight > 0) {
        // do all the gets first
        Status s = db->Get(options, key, &value);
        if (!s.ok() && !s.IsNotFound()) {
          fprintf(stderr, "get error: %s\n", s.ToString().c_str());
          // we continue after error rather than exiting so that we can
          // find more errors if any
        } else if (!s.IsNotFound()) {
          found++;
        }
        get_weight--;
        reads_done++;
      } else  if (put_weight > 0) {
        // then do all the corresponding number of puts
        // for all the gets we have done earlier
        Status s = db->Put(write_options_, key, gen.Generate(value_size_));
        if (!s.ok()) {
          fprintf(stderr, "put error: %s\n", s.ToString().c_str());
          exit(1);
        }
        put_weight--;
        writes_done++;
      }
      thread->stats.FinishedOps(db, 1);
    }
    char msg[100];
    snprintf(msg, sizeof(msg), "( reads:%" PRIu64 " writes:%" PRIu64 \
             " total:%" PRIu64 " found:%" PRIu64 ")",
             reads_done, writes_done, readwrites_, found);
    thread->stats.AddMessage(msg);
  }

  //
  // Read-modify-write for random keys
  void UpdateRandom(ThreadState* thread) {
    ReadOptions options(FLAGS_verify_checksum, true);
    RandomGenerator gen;
    std::string value;
    int64_t found = 0;
    Duration duration(FLAGS_duration, readwrites_);

    Slice key = AllocateKey();
    std::unique_ptr<const char[]> key_guard(key.data());
    // the number of iterations is the larger of read_ or write_
    while (!duration.Done(1)) {
      DB* db = SelectDB(thread);
      GenerateKeyFromInt(thread->rand.Next() % FLAGS_num, FLAGS_num, &key);

      if (db->Get(options, key, &value).ok()) {
        found++;
      }

      Status s = db->Put(write_options_, key, gen.Generate(value_size_));
      if (!s.ok()) {
        fprintf(stderr, "put error: %s\n", s.ToString().c_str());
        exit(1);
      }
      thread->stats.FinishedOps(db, 1);
    }
    char msg[100];
    snprintf(msg, sizeof(msg),
             "( updates:%" PRIu64 " found:%" PRIu64 ")", readwrites_, found);
    thread->stats.AddMessage(msg);
  }

  // Read-modify-write for random keys.
  // Each operation causes the key grow by value_size (simulating an append).
  // Generally used for benchmarking against merges of similar type
  void AppendRandom(ThreadState* thread) {
    ReadOptions options(FLAGS_verify_checksum, true);
    RandomGenerator gen;
    std::string value;
    int64_t found = 0;

    Slice key = AllocateKey();
    std::unique_ptr<const char[]> key_guard(key.data());
    // The number of iterations is the larger of read_ or write_
    Duration duration(FLAGS_duration, readwrites_);
    while (!duration.Done(1)) {
      DB* db = SelectDB(thread);
      GenerateKeyFromInt(thread->rand.Next() % FLAGS_num, FLAGS_num, &key);

      // Get the existing value
      if (db->Get(options, key, &value).ok()) {
        found++;
      } else {
        // If not existing, then just assume an empty string of data
        value.clear();
      }

      // Update the value (by appending data)
      Slice operand = gen.Generate(value_size_);
      if (value.size() > 0) {
        // Use a delimeter to match the semantics for StringAppendOperator
        value.append(1,',');
      }
      value.append(operand.data(), operand.size());

      // Write back to the database
      Status s = db->Put(write_options_, key, value);
      if (!s.ok()) {
        fprintf(stderr, "put error: %s\n", s.ToString().c_str());
        exit(1);
      }
      thread->stats.FinishedOps(db, 1);
    }

    char msg[100];
    snprintf(msg, sizeof(msg), "( updates:%" PRIu64 " found:%" PRIu64 ")",
            readwrites_, found);
    thread->stats.AddMessage(msg);
  }

  // Read-modify-write for random keys (using MergeOperator)
  // The merge operator to use should be defined by FLAGS_merge_operator
  // Adjust FLAGS_value_size so that the keys are reasonable for this operator
  // Assumes that the merge operator is non-null (i.e.: is well-defined)
  //
  // For example, use FLAGS_merge_operator="uint64add" and FLAGS_value_size=8
  // to simulate random additions over 64-bit integers using merge.
  //
  // The number of merges on the same key can be controlled by adjusting
  // FLAGS_merge_keys.
  void MergeRandom(ThreadState* thread) {
    RandomGenerator gen;

    Slice key = AllocateKey();
    std::unique_ptr<const char[]> key_guard(key.data());
    // The number of iterations is the larger of read_ or write_
    Duration duration(FLAGS_duration, readwrites_);
    while (!duration.Done(1)) {
      DB* db = SelectDB(thread);
      GenerateKeyFromInt(thread->rand.Next() % merge_keys_, merge_keys_, &key);

      Status s = db->Merge(write_options_, key, gen.Generate(value_size_));

      if (!s.ok()) {
        fprintf(stderr, "merge error: %s\n", s.ToString().c_str());
        exit(1);
      }
      thread->stats.FinishedOps(db, 1);
    }

    // Print some statistics
    char msg[100];
    snprintf(msg, sizeof(msg), "( updates:%" PRIu64 ")", readwrites_);
    thread->stats.AddMessage(msg);
  }

  // Read and merge random keys. The amount of reads and merges are controlled
  // by adjusting FLAGS_num and FLAGS_mergereadpercent. The number of distinct
  // keys (and thus also the number of reads and merges on the same key) can be
  // adjusted with FLAGS_merge_keys.
  //
  // As with MergeRandom, the merge operator to use should be defined by
  // FLAGS_merge_operator.
  void ReadRandomMergeRandom(ThreadState* thread) {
    ReadOptions options(FLAGS_verify_checksum, true);
    RandomGenerator gen;
    std::string value;
    int64_t num_hits = 0;
    int64_t num_gets = 0;
    int64_t num_merges = 0;
    size_t max_length = 0;

    Slice key = AllocateKey();
    std::unique_ptr<const char[]> key_guard(key.data());
    // the number of iterations is the larger of read_ or write_
    Duration duration(FLAGS_duration, readwrites_);
    while (!duration.Done(1)) {
      DB* db = SelectDB(thread);
      GenerateKeyFromInt(thread->rand.Next() % merge_keys_, merge_keys_, &key);

      bool do_merge = int(thread->rand.Next() % 100) < FLAGS_mergereadpercent;

      if (do_merge) {
        Status s = db->Merge(write_options_, key, gen.Generate(value_size_));
        if (!s.ok()) {
          fprintf(stderr, "merge error: %s\n", s.ToString().c_str());
          exit(1);
        }

        num_merges++;

      } else {
        Status s = db->Get(options, key, &value);
        if (value.length() > max_length)
          max_length = value.length();

        if (!s.ok() && !s.IsNotFound()) {
          fprintf(stderr, "get error: %s\n", s.ToString().c_str());
          // we continue after error rather than exiting so that we can
          // find more errors if any
        } else if (!s.IsNotFound()) {
          num_hits++;
        }

        num_gets++;

      }

      thread->stats.FinishedOps(db, 1);
    }

    char msg[100];
    snprintf(msg, sizeof(msg),
             "(reads:%" PRIu64 " merges:%" PRIu64 " total:%" PRIu64 " hits:%" \
             PRIu64 " maxlength:%zu)",
             num_gets, num_merges, readwrites_, num_hits, max_length);
    thread->stats.AddMessage(msg);
  }

  void Compact(ThreadState* thread) {
    DB* db = SelectDB(thread);
    db->CompactRange(nullptr, nullptr);
  }

  void PrintStats(const char* key) {
    if (db_.db != nullptr) {
      PrintStats(db_.db, key, false);
    }
    for (const auto& db_with_cfh : multi_dbs_) {
      PrintStats(db_with_cfh.db, key, true);
    }
  }

  void PrintStats(DB* db, const char* key, bool print_header = false) {
    if (print_header) {
      fprintf(stdout, "\n==== DB: %s ===\n", db->GetName().c_str());
    }
    std::string stats;
    if (!db->GetProperty(key, &stats)) {
      stats = "(failed)";
    }
    fprintf(stdout, "\n%s\n", stats.c_str());
  }
};

}  // namespace rocksdb

int main(int argc, char** argv) {
  rocksdb::port::InstallStackTraceHandler();
  SetUsageMessage(std::string("\nUSAGE:\n") + std::string(argv[0]) +
                  " [OPTIONS]...");
  ParseCommandLineFlags(&argc, &argv, true);

  FLAGS_compaction_style_e = (rocksdb::CompactionStyle) FLAGS_compaction_style;
  if (FLAGS_statistics) {
    dbstats = rocksdb::CreateDBStatistics();
  }

  std::vector<std::string> fanout =
    rocksdb::stringSplit(FLAGS_max_bytes_for_level_multiplier_additional, ',');
  for (unsigned int j= 0; j < fanout.size(); j++) {
    FLAGS_max_bytes_for_level_multiplier_additional_v.push_back(
      std::stoi(fanout[j]));
  }

  FLAGS_compression_type_e =
    StringToCompressionType(FLAGS_compression_type.c_str());

  if (!FLAGS_hdfs.empty()) {
    FLAGS_env  = new rocksdb::HdfsEnv(FLAGS_hdfs);
  }

  if (!strcasecmp(FLAGS_compaction_fadvice.c_str(), "NONE"))
    FLAGS_compaction_fadvice_e = rocksdb::Options::NONE;
  else if (!strcasecmp(FLAGS_compaction_fadvice.c_str(), "NORMAL"))
    FLAGS_compaction_fadvice_e = rocksdb::Options::NORMAL;
  else if (!strcasecmp(FLAGS_compaction_fadvice.c_str(), "SEQUENTIAL"))
    FLAGS_compaction_fadvice_e = rocksdb::Options::SEQUENTIAL;
  else if (!strcasecmp(FLAGS_compaction_fadvice.c_str(), "WILLNEED"))
    FLAGS_compaction_fadvice_e = rocksdb::Options::WILLNEED;
  else {
    fprintf(stdout, "Unknown compaction fadvice:%s\n",
            FLAGS_compaction_fadvice.c_str());
  }

  FLAGS_rep_factory = StringToRepFactory(FLAGS_memtablerep.c_str());

  // The number of background threads should be at least as much the
  // max number of concurrent compactions.
  FLAGS_env->SetBackgroundThreads(FLAGS_max_background_compactions);
  // Choose a location for the test database if none given with --db=<path>
  if (FLAGS_db.empty()) {
    std::string default_db_path;
    rocksdb::Env::Default()->GetTestDirectory(&default_db_path);
    default_db_path += "/dbbench";
    FLAGS_db = default_db_path;
  }

  rocksdb::Benchmark benchmark;
  benchmark.Run();
  return 0;
}

#endif  // GFLAGS
