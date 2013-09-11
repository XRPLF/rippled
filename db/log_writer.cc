// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "log_writer.h"

#include <stdint.h>
#include "../hyperleveldb/env.h"
#include "../util/coding.h"
#include "../util/crc32c.h"
#include "../util/mutexlock.h"

namespace hyperleveldb {
namespace log {

Writer::Writer(WritableFile* dest)
    : dest_(dest),
      offset_mtx_(),
      offset_(0) {
  for (int i = 0; i <= kMaxRecordType; i++) {
    char t = static_cast<char>(i);
    type_crc_[i] = crc32c::Value(&t, 1);
  }
}

Writer::~Writer() {
}

Status Writer::AddRecord(const Slice& slice) {
  // computation of block_offset requires a pow2
  assert(kBlockSize == 32768);
  uint64_t start_offset;
  uint64_t end_offset;

  {
    MutexLock l(&offset_mtx_);
    start_offset = offset_;
    end_offset = offset_;
    // compute the new offset_
    uint64_t left = slice.size();
    do {
      uint64_t block_offset = end_offset & (kBlockSize - 1);
      const uint64_t leftover = kBlockSize - block_offset;
      assert(leftover > 0);
      if (leftover < kHeaderSize) {
        end_offset += leftover;
        block_offset = 0;
      }
      // Invariant: we never leave < kHeaderSize bytes in a block.
      assert(kBlockSize - block_offset - kHeaderSize >= 0);

      const uint64_t avail = kBlockSize - block_offset - kHeaderSize;
      const uint64_t fragment_length = (left < avail) ? left : avail;

      end_offset += kHeaderSize + fragment_length;
      left -= fragment_length;
    } while (left > 0);
    offset_ = end_offset;
  }

  const char* ptr = slice.data();
  size_t left = slice.size();
  uint64_t offset = start_offset;

  // Fragment the record if necessary and emit it.  Note that if slice
  // is empty, we still want to iterate once to emit a single
  // zero-length record
  Status s;
  bool begin = true;
  do {
    uint64_t block_offset = offset & (kBlockSize - 1);
    const uint64_t leftover = kBlockSize - block_offset;
    assert(leftover > 0);
    if (leftover < kHeaderSize) {
      // Switch to a new block
      // Fill the trailer (literal below relies on kHeaderSize being 7)
      assert(kHeaderSize == 7);
      dest_->WriteAt(offset, Slice("\x00\x00\x00\x00\x00\x00", leftover));
      block_offset = 0;
      offset += leftover;
    }
    // Invariant: we never leave < kHeaderSize bytes in a block.
    assert(kBlockSize - block_offset - kHeaderSize >= 0);

    const size_t avail = kBlockSize - block_offset - kHeaderSize;
    const size_t fragment_length = (left < avail) ? left : avail;

    RecordType type;
    const bool end = (left == fragment_length);
    if (begin && end) {
      type = kFullType;
    } else if (begin) {
      type = kFirstType;
    } else if (end) {
      type = kLastType;
    } else {
      type = kMiddleType;
    }

    s = EmitPhysicalRecordAt(type, ptr, offset, fragment_length);
    offset += kHeaderSize + fragment_length;
    ptr += fragment_length;
    left -= fragment_length;
    begin = false;
  } while (s.ok() && left > 0);
  return s;
}

Status Writer::EmitPhysicalRecordAt(RecordType t, const char* ptr, uint64_t offset, size_t n) {
  assert(n <= 0xffff);  // Must fit in two bytes

  // Format the header
  char buf[kHeaderSize];
  buf[4] = static_cast<char>(n & 0xff);
  buf[5] = static_cast<char>(n >> 8);
  buf[6] = static_cast<char>(t);

  // Compute the crc of the record type and the payload.
  uint32_t crc = crc32c::Extend(type_crc_[t], ptr, n);
  crc = crc32c::Mask(crc);                 // Adjust for storage
  EncodeFixed32(buf, crc);

  // Write the header and the payload
  Status s = dest_->WriteAt(offset, Slice(buf, kHeaderSize));
  if (s.ok()) {
    s = dest_->WriteAt(offset + kHeaderSize, Slice(ptr, n));
  }
  return s;
}

}  // namespace log
}  // namespace hyperleveldb
