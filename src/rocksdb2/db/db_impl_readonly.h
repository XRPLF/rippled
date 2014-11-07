//  Copyright (c) 2013, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Copyright (c) 2012 Facebook. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once
#include "db/db_impl.h"

#include <deque>
#include <set>
#include <vector>
#include <string>
#include "db/dbformat.h"
#include "db/log_writer.h"
#include "db/snapshot.h"
#include "rocksdb/db.h"
#include "rocksdb/env.h"
#include "port/port.h"

namespace rocksdb {

class DBImplReadOnly : public DBImpl {
 public:
  DBImplReadOnly(const DBOptions& options, const std::string& dbname);
  virtual ~DBImplReadOnly();

  // Implementations of the DB interface
  using DB::Get;
  virtual Status Get(const ReadOptions& options,
                     ColumnFamilyHandle* column_family, const Slice& key,
                     std::string* value) override;

  // TODO: Implement ReadOnly MultiGet?

  using DBImpl::NewIterator;
  virtual Iterator* NewIterator(const ReadOptions&,
                                ColumnFamilyHandle* column_family) override;

  virtual Status NewIterators(
      const ReadOptions& options,
      const std::vector<ColumnFamilyHandle*>& column_families,
      std::vector<Iterator*>* iterators) override;

  using DBImpl::Put;
  virtual Status Put(const WriteOptions& options,
                     ColumnFamilyHandle* column_family, const Slice& key,
                     const Slice& value) override {
    return Status::NotSupported("Not supported operation in read only mode.");
  }
  using DBImpl::Merge;
  virtual Status Merge(const WriteOptions& options,
                       ColumnFamilyHandle* column_family, const Slice& key,
                       const Slice& value) override {
    return Status::NotSupported("Not supported operation in read only mode.");
  }
  using DBImpl::Delete;
  virtual Status Delete(const WriteOptions& options,
                        ColumnFamilyHandle* column_family,
                        const Slice& key) override {
    return Status::NotSupported("Not supported operation in read only mode.");
  }
  virtual Status Write(const WriteOptions& options,
                       WriteBatch* updates) override {
    return Status::NotSupported("Not supported operation in read only mode.");
  }
  using DBImpl::CompactRange;
  virtual Status CompactRange(ColumnFamilyHandle* column_family,
                              const Slice* begin, const Slice* end,
                              bool reduce_level = false, int target_level = -1,
                              uint32_t target_path_id = 0) override {
    return Status::NotSupported("Not supported operation in read only mode.");
  }

#ifndef ROCKSDB_LITE
  virtual Status DisableFileDeletions() override {
    return Status::NotSupported("Not supported operation in read only mode.");
  }
  virtual Status EnableFileDeletions(bool force) override {
    return Status::NotSupported("Not supported operation in read only mode.");
  }
  virtual Status GetLiveFiles(std::vector<std::string>&,
                              uint64_t* manifest_file_size,
                              bool flush_memtable = true) override {
    return Status::NotSupported("Not supported operation in read only mode.");
  }
#endif  // ROCKSDB_LITE

  using DBImpl::Flush;
  virtual Status Flush(const FlushOptions& options,
                       ColumnFamilyHandle* column_family) override {
    return Status::NotSupported("Not supported operation in read only mode.");
  }

 private:
  friend class DB;

  // No copying allowed
  DBImplReadOnly(const DBImplReadOnly&);
  void operator=(const DBImplReadOnly&);
};
}
