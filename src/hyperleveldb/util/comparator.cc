// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <algorithm>
#include <stdint.h>

#include "../hyperleveldb/comparator.h"
#include "../hyperleveldb/slice.h"
#include "../port/port.h"
#include "coding.h"
#include "logging.h"

namespace hyperleveldb {

Comparator::~Comparator() { }

uint64_t Comparator::KeyNum(const Slice& key) const {
  return 0;
}

namespace {
class BytewiseComparatorImpl : public Comparator {
 public:
  BytewiseComparatorImpl() { }

  virtual const char* Name() const {
    return "leveldb.BytewiseComparator";
  }

  virtual int Compare(const Slice& a, const Slice& b) const {
    return a.compare(b);
  }

  virtual void FindShortestSeparator(
      std::string* start,
      const Slice& limit) const {
    // Find length of common prefix
    size_t min_length = std::min(start->size(), limit.size());
    size_t diff_index = 0;
    while ((diff_index < min_length) &&
           ((*start)[diff_index] == limit[diff_index])) {
      diff_index++;
    }

    if (diff_index >= min_length) {
      // Do not shorten if one string is a prefix of the other
    } else {
      uint8_t diff_byte = static_cast<uint8_t>((*start)[diff_index]);
      if (diff_byte < static_cast<uint8_t>(0xff) &&
          diff_byte + 1 < static_cast<uint8_t>(limit[diff_index])) {
        (*start)[diff_index]++;
        start->resize(diff_index + 1);
        assert(Compare(*start, limit) < 0);
      }
    }
  }

  virtual void FindShortSuccessor(std::string* key) const {
    // Find first character that can be incremented
    size_t n = key->size();
    for (size_t i = 0; i < n; i++) {
      const uint8_t byte = (*key)[i];
      if (byte != static_cast<uint8_t>(0xff)) {
        (*key)[i] = byte + 1;
        key->resize(i+1);
        return;
      }
    }
    // *key is a run of 0xffs.  Leave it alone.
  }

  virtual uint64_t KeyNum(const Slice& key) const {
    unsigned char buf[sizeof(uint64_t)];
    memset(buf, 0, sizeof(buf));
    memmove(buf, key.data(), std::min(key.size(), sizeof(uint64_t)));
    uint64_t number;
    number = static_cast<uint64_t>(buf[0]) << 56
           | static_cast<uint64_t>(buf[1]) << 48
           | static_cast<uint64_t>(buf[2]) << 40
           | static_cast<uint64_t>(buf[3]) << 32
           | static_cast<uint64_t>(buf[4]) << 24
           | static_cast<uint64_t>(buf[5]) << 16
           | static_cast<uint64_t>(buf[6]) << 8
           | static_cast<uint64_t>(buf[7]);
    return number;
  }
};
}  // namespace

static port::OnceType onceComparator = LEVELDB_ONCE_INIT;
static const Comparator* bytewise;

static void InitModule() {
  bytewise = new BytewiseComparatorImpl;
}

const Comparator* BytewiseComparator() {
  port::InitOnce(&onceComparator, InitModule);
  return bytewise;
}

}  // namespace hyperleveldb
