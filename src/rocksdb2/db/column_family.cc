//  Copyright (c) 2013, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/column_family.h"

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <inttypes.h>
#include <vector>
#include <string>
#include <algorithm>
#include <limits>

#include "db/db_impl.h"
#include "db/version_set.h"
#include "db/internal_stats.h"
#include "db/compaction_picker.h"
#include "db/table_properties_collector.h"
#include "db/write_controller.h"
#include "util/autovector.h"
#include "util/hash_skiplist_rep.h"
#include "util/options_helper.h"

namespace rocksdb {

namespace {
// This function computes the amount of time in microseconds by which a write
// should be delayed based on the number of level-0 files according to the
// following formula:
// if n < bottom, return 0;
// if n >= top, return 1000;
// otherwise, let r = (n - bottom) /
//                    (top - bottom)
//  and return r^2 * 1000.
// The goal of this formula is to gradually increase the rate at which writes
// are slowed. We also tried linear delay (r * 1000), but it seemed to do
// slightly worse. There is no other particular reason for choosing quadratic.
uint64_t SlowdownAmount(int n, double bottom, double top) {
  uint64_t delay;
  if (n >= top) {
    delay = 1000;
  } else if (n < bottom) {
    delay = 0;
  } else {
    // If we are here, we know that:
    //   level0_start_slowdown <= n < level0_slowdown
    // since the previous two conditions are false.
    double how_much = static_cast<double>(n - bottom) / (top - bottom);
    delay = std::max(how_much * how_much * 1000, 100.0);
  }
  assert(delay <= 1000);
  return delay;
}
}  // namespace

ColumnFamilyHandleImpl::ColumnFamilyHandleImpl(ColumnFamilyData* cfd,
                                               DBImpl* db, port::Mutex* mutex)
    : cfd_(cfd), db_(db), mutex_(mutex) {
  if (cfd_ != nullptr) {
    cfd_->Ref();
  }
}

ColumnFamilyHandleImpl::~ColumnFamilyHandleImpl() {
  if (cfd_ != nullptr) {
    DBImpl::DeletionState deletion_state;
    mutex_->Lock();
    if (cfd_->Unref()) {
      delete cfd_;
    }
    db_->FindObsoleteFiles(deletion_state, false, true);
    mutex_->Unlock();
    if (deletion_state.HaveSomethingToDelete()) {
      db_->PurgeObsoleteFiles(deletion_state);
    }
  }
}

uint32_t ColumnFamilyHandleImpl::GetID() const { return cfd()->GetID(); }

const Comparator* ColumnFamilyHandleImpl::user_comparator() const {
  return cfd()->user_comparator();
}

ColumnFamilyOptions SanitizeOptions(const InternalKeyComparator* icmp,
                                    const ColumnFamilyOptions& src) {
  ColumnFamilyOptions result = src;
  result.comparator = icmp;
#ifdef OS_MACOSX
  // TODO(icanadi) make write_buffer_size uint64_t instead of size_t
  ClipToRange(&result.write_buffer_size, ((size_t)64) << 10, ((size_t)1) << 30);
#else
  ClipToRange(&result.write_buffer_size,
              ((size_t)64) << 10, ((size_t)64) << 30);
#endif
  // if user sets arena_block_size, we trust user to use this value. Otherwise,
  // calculate a proper value from writer_buffer_size;
  if (result.arena_block_size <= 0) {
    result.arena_block_size = result.write_buffer_size / 10;
  }
  result.min_write_buffer_number_to_merge =
      std::min(result.min_write_buffer_number_to_merge,
               result.max_write_buffer_number - 1);
  result.compression_per_level = src.compression_per_level;
  if (result.max_mem_compaction_level >= result.num_levels) {
    result.max_mem_compaction_level = result.num_levels - 1;
  }
  if (result.soft_rate_limit > result.hard_rate_limit) {
    result.soft_rate_limit = result.hard_rate_limit;
  }
  if (result.max_write_buffer_number < 2) {
    result.max_write_buffer_number = 2;
  }
  if (!result.prefix_extractor) {
    assert(result.memtable_factory);
    Slice name = result.memtable_factory->Name();
    if (name.compare("HashSkipListRepFactory") == 0 ||
        name.compare("HashLinkListRepFactory") == 0) {
      result.memtable_factory = std::make_shared<SkipListFactory>();
    }
  }

  // -- Sanitize the table properties collector
  // All user defined properties collectors will be wrapped by
  // UserKeyTablePropertiesCollector since for them they only have the
  // knowledge of the user keys; internal keys are invisible to them.
  auto& collector_factories = result.table_properties_collector_factories;
  for (size_t i = 0; i < result.table_properties_collector_factories.size();
       ++i) {
    assert(collector_factories[i]);
    collector_factories[i] =
        std::make_shared<UserKeyTablePropertiesCollectorFactory>(
            collector_factories[i]);
  }
  // Add collector to collect internal key statistics
  collector_factories.push_back(
      std::make_shared<InternalKeyPropertiesCollectorFactory>());

  if (result.compaction_style == kCompactionStyleFIFO) {
    result.num_levels = 1;
    // since we delete level0 files in FIFO compaction when there are too many
    // of them, these options don't really mean anything
    result.level0_file_num_compaction_trigger = std::numeric_limits<int>::max();
    result.level0_slowdown_writes_trigger = std::numeric_limits<int>::max();
    result.level0_stop_writes_trigger = std::numeric_limits<int>::max();
  }

  return result;
}

int SuperVersion::dummy = 0;
void* const SuperVersion::kSVInUse = &SuperVersion::dummy;
void* const SuperVersion::kSVObsolete = nullptr;

SuperVersion::~SuperVersion() {
  for (auto td : to_delete) {
    delete td;
  }
}

SuperVersion* SuperVersion::Ref() {
  refs.fetch_add(1, std::memory_order_relaxed);
  return this;
}

bool SuperVersion::Unref() {
  // fetch_sub returns the previous value of ref
  uint32_t previous_refs = refs.fetch_sub(1, std::memory_order_relaxed);
  assert(previous_refs > 0);
  return previous_refs == 1;
}

void SuperVersion::Cleanup() {
  assert(refs.load(std::memory_order_relaxed) == 0);
  imm->Unref(&to_delete);
  MemTable* m = mem->Unref();
  if (m != nullptr) {
    to_delete.push_back(m);
  }
  current->Unref();
}

void SuperVersion::Init(MemTable* new_mem, MemTableListVersion* new_imm,
                        Version* new_current) {
  mem = new_mem;
  imm = new_imm;
  current = new_current;
  mem->Ref();
  imm->Ref();
  current->Ref();
  refs.store(1, std::memory_order_relaxed);
}

namespace {
void SuperVersionUnrefHandle(void* ptr) {
  // UnrefHandle is called when a thread exists or a ThreadLocalPtr gets
  // destroyed. When former happens, the thread shouldn't see kSVInUse.
  // When latter happens, we are in ~ColumnFamilyData(), no get should happen as
  // well.
  SuperVersion* sv = static_cast<SuperVersion*>(ptr);
  if (sv->Unref()) {
    sv->db_mutex->Lock();
    sv->Cleanup();
    sv->db_mutex->Unlock();
    delete sv;
  }
}
}  // anonymous namespace

ColumnFamilyData::ColumnFamilyData(uint32_t id, const std::string& name,
                                   Version* dummy_versions, Cache* table_cache,
                                   const ColumnFamilyOptions& cf_options,
                                   const DBOptions* db_options,
                                   const EnvOptions& env_options,
                                   ColumnFamilySet* column_family_set)
    : id_(id),
      name_(name),
      dummy_versions_(dummy_versions),
      current_(nullptr),
      refs_(0),
      dropped_(false),
      internal_comparator_(cf_options.comparator),
      options_(*db_options, SanitizeOptions(&internal_comparator_, cf_options)),
      ioptions_(options_),
      mutable_cf_options_(options_),
      mem_(nullptr),
      imm_(options_.min_write_buffer_number_to_merge),
      super_version_(nullptr),
      super_version_number_(0),
      local_sv_(new ThreadLocalPtr(&SuperVersionUnrefHandle)),
      next_(nullptr),
      prev_(nullptr),
      log_number_(0),
      column_family_set_(column_family_set) {
  Ref();

  // if dummy_versions is nullptr, then this is a dummy column family.
  if (dummy_versions != nullptr) {
    internal_stats_.reset(
        new InternalStats(options_.num_levels, db_options->env, this));
    table_cache_.reset(new TableCache(ioptions_, env_options, table_cache));
    if (options_.compaction_style == kCompactionStyleUniversal) {
      compaction_picker_.reset(
          new UniversalCompactionPicker(&options_, &internal_comparator_));
    } else if (options_.compaction_style == kCompactionStyleLevel) {
      compaction_picker_.reset(
          new LevelCompactionPicker(&options_, &internal_comparator_));
    } else {
      assert(options_.compaction_style == kCompactionStyleFIFO);
      compaction_picker_.reset(
          new FIFOCompactionPicker(&options_, &internal_comparator_));
    }

    Log(options_.info_log, "Options for column family \"%s\":\n",
        name.c_str());
    const ColumnFamilyOptions* cf_options = &options_;
    cf_options->Dump(options_.info_log.get());
  }

  RecalculateWriteStallConditions();
}

// DB mutex held
ColumnFamilyData::~ColumnFamilyData() {
  assert(refs_ == 0);
  // remove from linked list
  auto prev = prev_;
  auto next = next_;
  prev->next_ = next;
  next->prev_ = prev;

  // it's nullptr for dummy CFD
  if (column_family_set_ != nullptr) {
    // remove from column_family_set
    column_family_set_->RemoveColumnFamily(this);
  }

  if (current_ != nullptr) {
    current_->Unref();
  }

  if (super_version_ != nullptr) {
    // Release SuperVersion reference kept in ThreadLocalPtr.
    // This must be done outside of mutex_ since unref handler can lock mutex.
    super_version_->db_mutex->Unlock();
    local_sv_.reset();
    super_version_->db_mutex->Lock();

    bool is_last_reference __attribute__((unused));
    is_last_reference = super_version_->Unref();
    assert(is_last_reference);
    super_version_->Cleanup();
    delete super_version_;
    super_version_ = nullptr;
  }

  if (dummy_versions_ != nullptr) {
    // List must be empty
    assert(dummy_versions_->next_ == dummy_versions_);
    delete dummy_versions_;
  }

  if (mem_ != nullptr) {
    delete mem_->Unref();
  }
  autovector<MemTable*> to_delete;
  imm_.current()->Unref(&to_delete);
  for (MemTable* m : to_delete) {
    delete m;
  }
}

void ColumnFamilyData::RecalculateWriteStallConditions() {
  if (current_ != nullptr) {
    const double score = current_->MaxCompactionScore();
    const int max_level = current_->MaxCompactionScoreLevel();

    auto write_controller = column_family_set_->write_controller_;

    if (imm()->size() == options_.max_write_buffer_number) {
      write_controller_token_ = write_controller->GetStopToken();
      internal_stats_->AddCFStats(InternalStats::MEMTABLE_COMPACTION, 1);
      Log(options_.info_log,
          "[%s] Stopping writes because we have %d immutable memtables "
          "(waiting for flush)",
          name_.c_str(), imm()->size());
    } else if (current_->NumLevelFiles(0) >=
               options_.level0_stop_writes_trigger) {
      write_controller_token_ = write_controller->GetStopToken();
      internal_stats_->AddCFStats(InternalStats::LEVEL0_NUM_FILES, 1);
      Log(options_.info_log,
          "[%s] Stopping writes because we have %d level-0 files",
          name_.c_str(), current_->NumLevelFiles(0));
    } else if (options_.level0_slowdown_writes_trigger >= 0 &&
               current_->NumLevelFiles(0) >=
                   options_.level0_slowdown_writes_trigger) {
      uint64_t slowdown = SlowdownAmount(
          current_->NumLevelFiles(0), options_.level0_slowdown_writes_trigger,
          options_.level0_stop_writes_trigger);
      write_controller_token_ = write_controller->GetDelayToken(slowdown);
      internal_stats_->AddCFStats(InternalStats::LEVEL0_SLOWDOWN, slowdown);
      Log(options_.info_log,
          "[%s] Stalling writes because we have %d level-0 files (%" PRIu64
          "us)",
          name_.c_str(), current_->NumLevelFiles(0), slowdown);
    } else if (options_.hard_rate_limit > 1.0 &&
               score > options_.hard_rate_limit) {
      uint64_t kHardLimitSlowdown = 1000;
      write_controller_token_ =
          write_controller->GetDelayToken(kHardLimitSlowdown);
      internal_stats_->RecordLevelNSlowdown(max_level, kHardLimitSlowdown,
                                            false);
      Log(options_.info_log,
          "[%s] Stalling writes because we hit hard limit on level %d. "
          "(%" PRIu64 "us)",
          name_.c_str(), max_level, kHardLimitSlowdown);
    } else if (options_.soft_rate_limit > 0.0 &&
               score > options_.soft_rate_limit) {
      uint64_t slowdown = SlowdownAmount(score, options_.soft_rate_limit,
                                         options_.hard_rate_limit);
      write_controller_token_ = write_controller->GetDelayToken(slowdown);
      internal_stats_->RecordLevelNSlowdown(max_level, slowdown, true);
      Log(options_.info_log,
          "[%s] Stalling writes because we hit soft limit on level %d (%" PRIu64
          "us)",
          name_.c_str(), max_level, slowdown);
    } else {
      write_controller_token_.reset();
    }
  }
}

const EnvOptions* ColumnFamilyData::soptions() const {
  return &(column_family_set_->env_options_);
}

void ColumnFamilyData::SetCurrent(Version* current) { current_ = current; }

void ColumnFamilyData::CreateNewMemtable(const MemTableOptions& moptions) {
  assert(current_ != nullptr);
  if (mem_ != nullptr) {
    delete mem_->Unref();
  }
  mem_ = new MemTable(internal_comparator_, ioptions_, moptions);
  mem_->Ref();
}

Compaction* ColumnFamilyData::PickCompaction(LogBuffer* log_buffer) {
  auto result = compaction_picker_->PickCompaction(current_, log_buffer);
  return result;
}

Compaction* ColumnFamilyData::CompactRange(int input_level, int output_level,
                                           uint32_t output_path_id,
                                           const InternalKey* begin,
                                           const InternalKey* end,
                                           InternalKey** compaction_end) {
  return compaction_picker_->CompactRange(current_, input_level, output_level,
                                          output_path_id, begin, end,
                                          compaction_end);
}

SuperVersion* ColumnFamilyData::GetReferencedSuperVersion(
    port::Mutex* db_mutex) {
  SuperVersion* sv = nullptr;
  sv = GetThreadLocalSuperVersion(db_mutex);
  sv->Ref();
  if (!ReturnThreadLocalSuperVersion(sv)) {
    sv->Unref();
  }
  return sv;
}

SuperVersion* ColumnFamilyData::GetThreadLocalSuperVersion(
    port::Mutex* db_mutex) {
  SuperVersion* sv = nullptr;
  // The SuperVersion is cached in thread local storage to avoid acquiring
  // mutex when SuperVersion does not change since the last use. When a new
  // SuperVersion is installed, the compaction or flush thread cleans up
  // cached SuperVersion in all existing thread local storage. To avoid
  // acquiring mutex for this operation, we use atomic Swap() on the thread
  // local pointer to guarantee exclusive access. If the thread local pointer
  // is being used while a new SuperVersion is installed, the cached
  // SuperVersion can become stale. In that case, the background thread would
  // have swapped in kSVObsolete. We re-check the value at when returning
  // SuperVersion back to thread local, with an atomic compare and swap.
  // The superversion will need to be released if detected to be stale.
  void* ptr = local_sv_->Swap(SuperVersion::kSVInUse);
  // Invariant:
  // (1) Scrape (always) installs kSVObsolete in ThreadLocal storage
  // (2) the Swap above (always) installs kSVInUse, ThreadLocal storage
  // should only keep kSVInUse before ReturnThreadLocalSuperVersion call
  // (if no Scrape happens).
  assert(ptr != SuperVersion::kSVInUse);
  sv = static_cast<SuperVersion*>(ptr);
  if (sv == SuperVersion::kSVObsolete ||
      sv->version_number != super_version_number_.load()) {
    RecordTick(options_.statistics.get(), NUMBER_SUPERVERSION_ACQUIRES);
    SuperVersion* sv_to_delete = nullptr;

    if (sv && sv->Unref()) {
      RecordTick(options_.statistics.get(), NUMBER_SUPERVERSION_CLEANUPS);
      db_mutex->Lock();
      // NOTE: underlying resources held by superversion (sst files) might
      // not be released until the next background job.
      sv->Cleanup();
      sv_to_delete = sv;
    } else {
      db_mutex->Lock();
    }
    sv = super_version_->Ref();
    db_mutex->Unlock();

    delete sv_to_delete;
  }
  assert(sv != nullptr);
  return sv;
}

bool ColumnFamilyData::ReturnThreadLocalSuperVersion(SuperVersion* sv) {
  assert(sv != nullptr);
  // Put the SuperVersion back
  void* expected = SuperVersion::kSVInUse;
  if (local_sv_->CompareAndSwap(static_cast<void*>(sv), expected)) {
    // When we see kSVInUse in the ThreadLocal, we are sure ThreadLocal
    // storage has not been altered and no Scrape has happend. The
    // SuperVersion is still current.
    return true;
  } else {
    // ThreadLocal scrape happened in the process of this GetImpl call (after
    // thread local Swap() at the beginning and before CompareAndSwap()).
    // This means the SuperVersion it holds is obsolete.
    assert(expected == SuperVersion::kSVObsolete);
  }
  return false;
}

SuperVersion* ColumnFamilyData::InstallSuperVersion(
    SuperVersion* new_superversion, port::Mutex* db_mutex) {
  db_mutex->AssertHeld();
  return InstallSuperVersion(new_superversion, db_mutex, mutable_cf_options_);
}

SuperVersion* ColumnFamilyData::InstallSuperVersion(
    SuperVersion* new_superversion, port::Mutex* db_mutex,
    const MutableCFOptions& mutable_cf_options) {
  new_superversion->db_mutex = db_mutex;
  new_superversion->mutable_cf_options = mutable_cf_options;
  new_superversion->Init(mem_, imm_.current(), current_);
  SuperVersion* old_superversion = super_version_;
  super_version_ = new_superversion;
  ++super_version_number_;
  super_version_->version_number = super_version_number_;
  // Reset SuperVersions cached in thread local storage
  ResetThreadLocalSuperVersions();

  RecalculateWriteStallConditions();

  if (old_superversion != nullptr && old_superversion->Unref()) {
    old_superversion->Cleanup();
    return old_superversion;  // will let caller delete outside of mutex
  }
  return nullptr;
}

void ColumnFamilyData::ResetThreadLocalSuperVersions() {
  autovector<void*> sv_ptrs;
  local_sv_->Scrape(&sv_ptrs, SuperVersion::kSVObsolete);
  for (auto ptr : sv_ptrs) {
    assert(ptr);
    if (ptr == SuperVersion::kSVInUse) {
      continue;
    }
    auto sv = static_cast<SuperVersion*>(ptr);
    if (sv->Unref()) {
      sv->Cleanup();
      delete sv;
    }
  }
}

bool ColumnFamilyData::SetOptions(
      const std::unordered_map<std::string, std::string>& options_map) {
  MutableCFOptions new_mutable_cf_options;
  if (GetMutableOptionsFromStrings(mutable_cf_options_, options_map,
                                   &new_mutable_cf_options)) {
    mutable_cf_options_ = new_mutable_cf_options;
    return true;
  }
  return false;
}

ColumnFamilySet::ColumnFamilySet(const std::string& dbname,
                                 const DBOptions* db_options,
                                 const EnvOptions& env_options,
                                 Cache* table_cache,
                                 WriteController* write_controller)
    : max_column_family_(0),
      dummy_cfd_(new ColumnFamilyData(0, "", nullptr, nullptr,
                                      ColumnFamilyOptions(), db_options,
                                      env_options, nullptr)),
      default_cfd_cache_(nullptr),
      db_name_(dbname),
      db_options_(db_options),
      env_options_(env_options),
      table_cache_(table_cache),
      write_controller_(write_controller),
      spin_lock_(ATOMIC_FLAG_INIT) {
  // initialize linked list
  dummy_cfd_->prev_ = dummy_cfd_;
  dummy_cfd_->next_ = dummy_cfd_;
}

ColumnFamilySet::~ColumnFamilySet() {
  while (column_family_data_.size() > 0) {
    // cfd destructor will delete itself from column_family_data_
    auto cfd = column_family_data_.begin()->second;
    cfd->Unref();
    delete cfd;
  }
  dummy_cfd_->Unref();
  delete dummy_cfd_;
}

ColumnFamilyData* ColumnFamilySet::GetDefault() const {
  assert(default_cfd_cache_ != nullptr);
  return default_cfd_cache_;
}

ColumnFamilyData* ColumnFamilySet::GetColumnFamily(uint32_t id) const {
  auto cfd_iter = column_family_data_.find(id);
  if (cfd_iter != column_family_data_.end()) {
    return cfd_iter->second;
  } else {
    return nullptr;
  }
}

ColumnFamilyData* ColumnFamilySet::GetColumnFamily(const std::string& name)
    const {
  auto cfd_iter = column_families_.find(name);
  if (cfd_iter != column_families_.end()) {
    auto cfd = GetColumnFamily(cfd_iter->second);
    assert(cfd != nullptr);
    return cfd;
  } else {
    return nullptr;
  }
}

uint32_t ColumnFamilySet::GetNextColumnFamilyID() {
  return ++max_column_family_;
}

uint32_t ColumnFamilySet::GetMaxColumnFamily() { return max_column_family_; }

void ColumnFamilySet::UpdateMaxColumnFamily(uint32_t new_max_column_family) {
  max_column_family_ = std::max(new_max_column_family, max_column_family_);
}

size_t ColumnFamilySet::NumberOfColumnFamilies() const {
  return column_families_.size();
}

// under a DB mutex
ColumnFamilyData* ColumnFamilySet::CreateColumnFamily(
    const std::string& name, uint32_t id, Version* dummy_versions,
    const ColumnFamilyOptions& options) {
  assert(column_families_.find(name) == column_families_.end());
  ColumnFamilyData* new_cfd =
      new ColumnFamilyData(id, name, dummy_versions, table_cache_, options,
                           db_options_, env_options_, this);
  Lock();
  column_families_.insert({name, id});
  column_family_data_.insert({id, new_cfd});
  Unlock();
  max_column_family_ = std::max(max_column_family_, id);
  // add to linked list
  new_cfd->next_ = dummy_cfd_;
  auto prev = dummy_cfd_->prev_;
  new_cfd->prev_ = prev;
  prev->next_ = new_cfd;
  dummy_cfd_->prev_ = new_cfd;
  if (id == 0) {
    default_cfd_cache_ = new_cfd;
  }
  return new_cfd;
}

void ColumnFamilySet::Lock() {
  // spin lock
  while (spin_lock_.test_and_set(std::memory_order_acquire)) {
  }
}

void ColumnFamilySet::Unlock() { spin_lock_.clear(std::memory_order_release); }

// REQUIRES: DB mutex held
void ColumnFamilySet::FreeDeadColumnFamilies() {
  autovector<ColumnFamilyData*> to_delete;
  for (auto cfd = dummy_cfd_->next_; cfd != dummy_cfd_; cfd = cfd->next_) {
    if (cfd->refs_ == 0) {
      to_delete.push_back(cfd);
    }
  }
  for (auto cfd : to_delete) {
    // this is very rare, so it's not a problem that we do it under a mutex
    delete cfd;
  }
}

// under a DB mutex
void ColumnFamilySet::RemoveColumnFamily(ColumnFamilyData* cfd) {
  auto cfd_iter = column_family_data_.find(cfd->GetID());
  assert(cfd_iter != column_family_data_.end());
  Lock();
  column_family_data_.erase(cfd_iter);
  column_families_.erase(cfd->GetName());
  Unlock();
}

bool ColumnFamilyMemTablesImpl::Seek(uint32_t column_family_id) {
  if (column_family_id == 0) {
    // optimization for common case
    current_ = column_family_set_->GetDefault();
  } else {
    // maybe outside of db mutex, should lock
    column_family_set_->Lock();
    current_ = column_family_set_->GetColumnFamily(column_family_id);
    column_family_set_->Unlock();
    // TODO(icanadi) Maybe remove column family from the hash table when it's
    // dropped?
    if (current_ != nullptr && current_->IsDropped()) {
      current_ = nullptr;
    }
  }
  handle_.SetCFD(current_);
  return current_ != nullptr;
}

uint64_t ColumnFamilyMemTablesImpl::GetLogNumber() const {
  assert(current_ != nullptr);
  return current_->GetLogNumber();
}

MemTable* ColumnFamilyMemTablesImpl::GetMemTable() const {
  assert(current_ != nullptr);
  return current_->mem();
}

const Options* ColumnFamilyMemTablesImpl::GetOptions() const {
  assert(current_ != nullptr);
  return current_->options();
}

ColumnFamilyHandle* ColumnFamilyMemTablesImpl::GetColumnFamilyHandle() {
  assert(current_ != nullptr);
  return &handle_;
}

void ColumnFamilyMemTablesImpl::CheckMemtableFull() {
  if (current_ != nullptr && current_->mem()->ShouldScheduleFlush()) {
    flush_scheduler_->ScheduleFlush(current_);
    current_->mem()->MarkFlushScheduled();
  }
}

uint32_t GetColumnFamilyID(ColumnFamilyHandle* column_family) {
  uint32_t column_family_id = 0;
  if (column_family != nullptr) {
    auto cfh = reinterpret_cast<ColumnFamilyHandleImpl*>(column_family);
    column_family_id = cfh->GetID();
  }
  return column_family_id;
}

const Comparator* GetColumnFamilyUserComparator(
    ColumnFamilyHandle* column_family) {
  if (column_family != nullptr) {
    auto cfh = reinterpret_cast<ColumnFamilyHandleImpl*>(column_family);
    return cfh->user_comparator();
  }
  return nullptr;
}

}  // namespace rocksdb
