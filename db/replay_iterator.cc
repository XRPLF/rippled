// Copyright (c) 2013 The HyperLevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "replay_iterator.h"

#include "filename.h"
#include "db_impl.h"
#include "dbformat.h"
#include "../hyperleveldb/env.h"
#include "../hyperleveldb/iterator.h"
#include "../port/port.h"
#include "../util/logging.h"
#include "../util/mutexlock.h"
#include "../util/random.h"

namespace hyperleveldb {

ReplayIterator::ReplayIterator() {
}

ReplayIterator::~ReplayIterator() {
}

ReplayState::ReplayState(Iterator* i, SequenceNumber s, SequenceNumber l)
  : mem_(NULL),
    iter_(i),
    seq_start_(s),
    seq_limit_(l) {
}

ReplayState::ReplayState(MemTable* m, SequenceNumber s)
  : mem_(m),
    iter_(NULL),
    seq_start_(s),
    seq_limit_(0) {
}

ReplayIteratorImpl::ReplayIteratorImpl(DBImpl* db, port::Mutex* mutex, const Comparator* cmp,
    Iterator* iter, MemTable* m, SequenceNumber s)
  : ReplayIterator(),
    db_(db),
    mutex_(mutex),
    user_comparator_(cmp),
    start_at_(s),
    valid_(),
    status_(),
    has_current_user_key_(false),
    current_user_key_(),
    current_user_sequence_(),
    rs_(iter, s, kMaxSequenceNumber),
    mems_() {
  m->Ref();
  mems_.push_back(ReplayState(m, s));
}

ReplayIteratorImpl::~ReplayIteratorImpl() {
}

bool ReplayIteratorImpl::Valid() {
  Prime();
  return valid_;
}

void ReplayIteratorImpl::Next() {
  rs_.iter_->Next();
}

bool ReplayIteratorImpl::HasValue() {
  ParsedInternalKey ikey;
  return ParseKey(&ikey) && ikey.type == kTypeValue;
}

Slice ReplayIteratorImpl::key() const {
  assert(valid_);
  return ExtractUserKey(rs_.iter_->key());
}

Slice ReplayIteratorImpl::value() const {
  assert(valid_);
  return rs_.iter_->value();
}

Status ReplayIteratorImpl::status() const {
  if (!status_.ok()) {
    return status_;
  } else {
    return rs_.iter_->status();
  }
}

void ReplayIteratorImpl::enqueue(MemTable* m, SequenceNumber s) {
  m->Ref();
  mems_.push_back(ReplayState(m, s));
}

void ReplayIteratorImpl::cleanup() {
  mutex_->Unlock();
  if (rs_.iter_) {
    delete rs_.iter_;
  }
  if (rs_.mem_) {
    rs_.mem_->Unref();
  }
  mutex_->Lock();
  rs_.iter_ = NULL;
  rs_.mem_ = NULL;

  while (!mems_.empty()) {
    MemTable* mem = mems_.front().mem_;
    Iterator* iter = mems_.front().iter_;
    mutex_->Unlock();
    if (iter) {
      delete iter;
    }
    if (mem) {
      mem->Unref();
    }
    mutex_->Lock();
    mems_.pop_front();
  }

  delete this;
}

bool ReplayIteratorImpl::ParseKey(ParsedInternalKey* ikey) {
  return ParseKey(rs_.iter_->key(), ikey);
}

bool ReplayIteratorImpl::ParseKey(const Slice& k, ParsedInternalKey* ikey) {
  if (!ParseInternalKey(k, ikey)) {
    status_ = Status::Corruption("corrupted internal key in ReplayIteratorImpl");
    return false;
  } else {
    return true;
  }
}

void ReplayIteratorImpl::Prime() {
  valid_ = false;
  if (!status_.ok()) {
    return;
  }
  while (true) {
    assert(rs_.iter_);
    while (rs_.iter_->Valid()) {
      ParsedInternalKey ikey;
      if (!ParseKey(rs_.iter_->key(), &ikey)) {
        return;
      }
      // if we can consider this key, and it's recent enough and of the right
      // type
      if ((!has_current_user_key_ ||
           user_comparator_->Compare(ikey.user_key,
                                     Slice(current_user_key_)) != 0 ||
           ikey.sequence >= current_user_sequence_) &&
          (ikey.sequence >= rs_.seq_start_ &&
            (ikey.type == kTypeDeletion || ikey.type == kTypeValue))) {
        has_current_user_key_ = true;
        current_user_key_.assign(ikey.user_key.data(), ikey.user_key.size());
        current_user_sequence_ = ikey.sequence;
        valid_ = true;
        return;
      }
      rs_.iter_->Next();
    }
    if (!rs_.iter_->status().ok()) {
      status_ = rs_.iter_->status();
      valid_ = false;
      return;
    }
    // we're done with rs_.iter_
    has_current_user_key_ = false;
    current_user_key_.assign("", 0);
    current_user_sequence_ = kMaxSequenceNumber;
    delete rs_.iter_;
    rs_.iter_ = NULL;
    {
      MutexLock l(mutex_);
      if (mems_.empty() ||
          rs_.seq_limit_ < mems_.front().seq_start_) {
        rs_.seq_start_ = rs_.seq_limit_;
      } else {
        if (rs_.mem_) {
          rs_.mem_->Unref();
          rs_.mem_ = NULL;
        }
        rs_.mem_ = mems_.front().mem_;
        rs_.seq_start_ = mems_.front().seq_start_;
        mems_.pop_front();
      }
    }
    rs_.seq_limit_ = db_->LastSequence();
    rs_.iter_ = rs_.mem_->NewIterator();
    rs_.iter_->SeekToFirst();
    assert(rs_.seq_start_ <= rs_.seq_limit_);
    if (rs_.seq_start_ == rs_.seq_limit_) {
      valid_ = false;
      return;
    }
  }
}

}  // namespace leveldb
