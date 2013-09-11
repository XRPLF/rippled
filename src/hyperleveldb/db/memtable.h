// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_HYPERLEVELDB_DB_MEMTABLE_H_
#define STORAGE_HYPERLEVELDB_DB_MEMTABLE_H_

#include <string>
#include "../hyperleveldb/db.h"
#include "dbformat.h"
#include "skiplist.h"
#include "../util/arena.h"

namespace hyperleveldb {

class InternalKeyComparator;
class Mutex;
class MemTableIterator;

class MemTable {
 public:
  // MemTables are reference counted.  The initial reference count
  // is zero and the caller must call Ref() at least once.
  explicit MemTable(const InternalKeyComparator& comparator);

  // Increase reference count.
  // XXX use a release increment if not using GCC intrinsics
  void Ref() { __sync_add_and_fetch(&refs_, 1); }

  // Drop reference count.  Delete if no more references exist.
  // XXX use an acquire decrement if not using GCC intrinsics
  void Unref() {
    int refs = __sync_sub_and_fetch(&refs_, 1);
    assert(refs >= 0);
    if (refs <= 0) {
      delete this;
    }
  }

  // Returns an estimate of the number of bytes of data in use by this
  // data structure.
  //
  // REQUIRES: external synchronization to prevent simultaneous
  // operations on the same MemTable.
  size_t ApproximateMemoryUsage();

  // Return an iterator that yields the contents of the memtable.
  //
  // The caller must ensure that the underlying MemTable remains live
  // while the returned iterator is live.  The keys returned by this
  // iterator are internal keys encoded by AppendInternalKey in the
  // db/format.{h,cc} module.
  Iterator* NewIterator();

  // Add an entry into memtable that maps key to value at the
  // specified sequence number and with the specified type.
  // Typically value will be empty if type==kTypeDeletion.
  void Add(SequenceNumber seq, ValueType type,
           const Slice& key,
           const Slice& value);

  // If memtable contains a value for key, store it in *value and return true.
  // If memtable contains a deletion for key, store a NotFound() error
  // in *status and return true.
  // Else, return false.
  bool Get(const LookupKey& key, std::string* value, Status* s);

 private:
  ~MemTable();  // Private since only Unref() should be used to delete it

  struct KeyComparator {
    const InternalKeyComparator comparator;
    explicit KeyComparator(const InternalKeyComparator& c) : comparator(c) { }
    int operator()(const char* a, const char* b) const;
  };
  friend class MemTableIterator;
  friend class MemTableBackwardIterator;

  typedef SkipList<const char*, KeyComparator> Table;

  KeyComparator comparator_;
  int refs_;
  port::Mutex mtx_;
  Arena arena_;
  Table table_;

  // No copying allowed
  MemTable(const MemTable&);
  void operator=(const MemTable&);
};

}  // namespace hyperleveldb

#endif  // STORAGE_HYPERLEVELDB_DB_MEMTABLE_H_
