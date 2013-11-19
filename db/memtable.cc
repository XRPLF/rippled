// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "memtable.h"
#include "dbformat.h"
#include "../hyperleveldb/comparator.h"
#include "../hyperleveldb/env.h"
#include "../hyperleveldb/iterator.h"
#include "../util/coding.h"
#include "../util/mutexlock.h"

namespace hyperleveldb {

static Slice GetLengthPrefixedSlice(const char* data) {
  uint32_t len;
  const char* p = data;
  p = GetVarint32Ptr(p, p + 5, &len);  // +5: we assume "p" is not corrupted
  return Slice(p, len);
}

static Slice GetLengthPrefixedSlice(std::pair<uint64_t, const char*> tk) {
  return GetLengthPrefixedSlice(tk.second);
}

MemTable::MemTable(const InternalKeyComparator& cmp)
    : comparator_(cmp),
      refs_(0),
      table_(comparator_, &arena_) {
}

MemTable::~MemTable() {
  assert(refs_ == 0);
}

size_t MemTable::ApproximateMemoryUsage() {
  MutexLock l(&mtx_);
  return arena_.MemoryUsage();
}

int MemTable::KeyComparator::operator()(TableKey ak, TableKey bk)
    const {
  if (ak.first < bk.first) {
    return -1;
  } else if (ak.first > bk.first) {
    return 1;
  }
  // Internal keys are encoded as length-prefixed strings.
  Slice a = GetLengthPrefixedSlice(ak);
  Slice b = GetLengthPrefixedSlice(bk);
  return comparator.Compare(a, b);
}

// Encode a suitable internal key target for "target" and return it.
// Uses *scratch as scratch space, and the returned pointer will point
// into this scratch space.
static const char* EncodeKey(std::string* scratch, const Slice& target) {
  scratch->clear();
  PutVarint32(scratch, target.size());
  scratch->append(target.data(), target.size());
  return scratch->data();
}

class MemTableIterator: public Iterator {
 public:
  explicit MemTableIterator(MemTable::Table* table,
                            MemTable::KeyComparator* cmp)
    : iter_(table), comparator_(cmp) { }

  virtual bool Valid() const { return iter_.Valid(); }
  virtual void Seek(const Slice& k) {
    uint64_t keynum = comparator_->comparator.user_comparator()->KeyNum(Slice(k.data(), k.size() - 8));
    iter_.Seek(std::make_pair(keynum, EncodeKey(&tmp_, k)));
  }
  virtual void SeekToFirst() { iter_.SeekToFirst(); }
  virtual void SeekToLast() { iter_.SeekToLast(); }
  virtual void Next() { iter_.Next(); }
  virtual void Prev() { iter_.Prev(); }
  virtual Slice key() const { return GetLengthPrefixedSlice(iter_.key()); }
  virtual Slice value() const {
    Slice key_slice = GetLengthPrefixedSlice(iter_.key());
    return GetLengthPrefixedSlice(key_slice.data() + key_slice.size());
  }

  virtual Status status() const { return Status::OK(); }

 private:
  MemTable::Table::Iterator iter_;
  MemTable::KeyComparator* comparator_;
  std::string tmp_;       // For passing to EncodeKey

  // No copying allowed
  MemTableIterator(const MemTableIterator&);
  void operator=(const MemTableIterator&);
};

Iterator* MemTable::NewIterator() {
  return new MemTableIterator(&table_, &comparator_);
}

void MemTable::Add(SequenceNumber s, ValueType type,
                   const Slice& key,
                   const Slice& value) {
  // Format of an entry is concatenation of:
  //  key_size     : varint32 of internal_key.size()
  //  key bytes    : char[internal_key.size()]
  //  value_size   : varint32 of value.size()
  //  value bytes  : char[value.size()]
  size_t key_size = key.size();
  size_t val_size = value.size();
  size_t internal_key_size = key_size + 8;
  const size_t encoded_len =
      VarintLength(internal_key_size) + internal_key_size +
      VarintLength(val_size) + val_size;
  char* buf = NULL;

  {
    MutexLock l(&mtx_);
    buf = arena_.Allocate(encoded_len);
  }

  char* p = EncodeVarint32(buf, internal_key_size);
  memcpy(p, key.data(), key_size);
  p += key_size;
  EncodeFixed64(p, (s << 8) | type);
  p += 8;
  p = EncodeVarint32(p, val_size);
  memcpy(p, value.data(), val_size);
  assert((p + val_size) - buf == encoded_len);
  uint64_t keynum = comparator_.comparator.user_comparator()->KeyNum(key);
  TableKey tk(keynum, buf);
  Table::InsertHint ih(&table_, tk);

  {
    MutexLock l(&mtx_);
    table_.InsertWithHint(&ih, tk);
  }
}

bool MemTable::Get(const LookupKey& key, std::string* value, Status* s) {
  Slice memkey = key.memtable_key();
  Table::Iterator iter(&table_);
  uint64_t keynum = comparator_.comparator.user_comparator()->KeyNum(key.user_key());
  TableKey tk(keynum, memkey.data());
  iter.Seek(tk);
  if (iter.Valid()) {
    // entry format is:
    //    klength  varint32
    //    userkey  char[klength]
    //    tag      uint64
    //    vlength  varint32
    //    value    char[vlength]
    // Check that it belongs to same user key.  We do not check the
    // sequence number since the Seek() call above should have skipped
    // all entries with overly large sequence numbers.
    const char* entry = iter.key().second;
    uint32_t key_length;
    const char* key_ptr = GetVarint32Ptr(entry, entry+5, &key_length);
    if (iter.key().first == tk.first &&
        comparator_.comparator.user_comparator()->Compare(
            Slice(key_ptr, key_length - 8),
            key.user_key()) == 0) {
      // Correct user key
      const uint64_t tag = DecodeFixed64(key_ptr + key_length - 8);
      switch (static_cast<ValueType>(tag & 0xff)) {
        case kTypeValue: {
          Slice v = GetLengthPrefixedSlice(key_ptr + key_length);
          value->assign(v.data(), v.size());
          return true;
        }
        case kTypeDeletion:
          *s = Status::NotFound(Slice());
          return true;
      }
    }
  }
  return false;
}

}  // namespace hyperleveldb
