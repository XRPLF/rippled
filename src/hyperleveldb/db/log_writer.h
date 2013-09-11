// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_HYPERLEVELDB_DB_LOG_WRITER_H_
#define STORAGE_HYPERLEVELDB_DB_LOG_WRITER_H_

#include <stdint.h>
#include "log_format.h"
#include "../hyperleveldb/slice.h"
#include "../hyperleveldb/status.h"
#include "../port/port.h"

namespace hyperleveldb {

class WritableFile;

namespace log {

class Writer {
 public:
  // Create a writer that will append data to "*dest".
  // "*dest" must be initially empty.
  // "*dest" must remain live while this Writer is in use.
  explicit Writer(WritableFile* dest);
  ~Writer();

  Status AddRecord(const Slice& slice);

 private:
  WritableFile* dest_;
  port::Mutex offset_mtx_;
  uint64_t offset_; // Current offset in file

  // crc32c values for all supported record types.  These are
  // pre-computed to reduce the overhead of computing the crc of the
  // record type stored in the header.
  uint32_t type_crc_[kMaxRecordType + 1];

  Status EmitPhysicalRecordAt(RecordType type, const char* ptr, uint64_t offset, size_t length);

  // No copying allowed
  Writer(const Writer&);
  void operator=(const Writer&);
};

}  // namespace log
}  // namespace hyperleveldb

#endif  // STORAGE_HYPERLEVELDB_DB_LOG_WRITER_H_
