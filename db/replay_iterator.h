// Copyright (c) 2013 The HyperLevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_HYPERLEVELDB_DB_ROLLING_ITERATOR_H_
#define STORAGE_HYPERLEVELDB_DB_ROLLING_ITERATOR_H_

#include <stdint.h>
#include <list>
#include "../hyperleveldb/db.h"
#include "../hyperleveldb/replay_iterator.h"
#include "dbformat.h"
#include "memtable.h"

namespace hyperleveldb {

class DBImpl;
struct ReplayState {
  ReplayState(Iterator* i, SequenceNumber s, SequenceNumber l);
  ReplayState(MemTable* m, SequenceNumber s);
  MemTable* mem_;
  Iterator* iter_;
  SequenceNumber seq_start_;
  SequenceNumber seq_limit_;
};

class ReplayIteratorImpl : public ReplayIterator {
 public:
  // Refs the memtable on its own; caller must hold mutex while creating this
  ReplayIteratorImpl(DBImpl* db, port::Mutex* mutex, const Comparator* cmp,
      Iterator* iter, MemTable* m, SequenceNumber s);
  virtual bool Valid();
  virtual void Next();
  virtual bool HasValue();
  virtual Slice key() const;
  virtual Slice value() const;
  virtual Status status() const;

  // extra interface

  // we ref the memtable; caller holds mutex passed into ctor
  // REQUIRES: caller must hold mutex passed into ctor
  void enqueue(MemTable* m, SequenceNumber s);

  // REQUIRES: caller must hold mutex passed into ctor
  void cleanup(); // calls delete this;

 private:
  virtual ~ReplayIteratorImpl();
  bool ParseKey(ParsedInternalKey* ikey);
  bool ParseKey(const Slice& k, ParsedInternalKey* ikey);
  void Prime();

  DBImpl* const db_;
  port::Mutex* mutex_;
  const Comparator* const user_comparator_;
  SequenceNumber const start_at_;
  bool valid_;
  Status status_;

  bool has_current_user_key_;
  std::string current_user_key_;
  SequenceNumber current_user_sequence_;

  ReplayState rs_;
  std::list<ReplayState> mems_;
};

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_DB_ROLLING_ITERATOR_H_
