//  Copyright (c) 2013, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.

#ifndef ROCKSDB_LITE
#include "db/tailing_iter.h"

#include <string>
#include <utility>
#include <vector>
#include "db/db_impl.h"
#include "db/db_iter.h"
#include "db/column_family.h"
#include "rocksdb/env.h"
#include "rocksdb/slice.h"
#include "rocksdb/slice_transform.h"
#include "table/merger.h"

namespace rocksdb {

TailingIterator::TailingIterator(Env* const env, DBImpl* db,
    const ReadOptions& read_options, ColumnFamilyData* cfd)
    : env_(env),
      db_(db),
      read_options_(read_options),
      cfd_(cfd),
      super_version_(nullptr),
      current_(nullptr),
      status_(Status::InvalidArgument("Seek() not called on this iterator")) {}

TailingIterator::~TailingIterator() {
  Cleanup();
}

bool TailingIterator::Valid() const {
  return current_ != nullptr;
}

void TailingIterator::SeekToFirst() {
  if (!IsCurrentVersion()) {
    CreateIterators();
  }

  mutable_->SeekToFirst();
  immutable_->SeekToFirst();
  UpdateCurrent();
}

void TailingIterator::Seek(const Slice& target) {
  if (!IsCurrentVersion()) {
    CreateIterators();
  }

  mutable_->Seek(target);

  // We maintain the interval (prev_key_, immutable_->key()] such that there
  // are no records with keys within that range in immutable_ other than
  // immutable_->key(). Since immutable_ can't change in this version, we don't
  // need to do a seek if 'target' belongs to that interval (i.e. immutable_ is
  // already at the correct position)!
  //
  // If prefix seek is used and immutable_ is not valid, seek if target has a
  // different prefix than prev_key.
  //
  // prev_key_ is updated by Next(). SeekImmutable() sets prev_key_ to
  // 'target' -- in this case, prev_key_ is included in the interval, so
  // prev_inclusive_ has to be set.

  const Comparator* cmp = cfd_->user_comparator();
  if (!is_prev_set_ || cmp->Compare(prev_key_, target) >= !is_prev_inclusive_ ||
      (immutable_->Valid() && cmp->Compare(target, immutable_->key()) > 0) ||
      (cfd_->options()->prefix_extractor != nullptr && !IsSamePrefix(target))) {
    SeekImmutable(target);
  }

  UpdateCurrent();
}

void TailingIterator::Next() {
  assert(Valid());

  if (!IsCurrentVersion()) {
    // save the current key, create new iterators and then seek
    std::string current_key = key().ToString();
    Slice key_slice(current_key.data(), current_key.size());

    CreateIterators();
    Seek(key_slice);

    if (!Valid() || key().compare(key_slice) != 0) {
      // record with current_key no longer exists
      return;
    }

  } else if (current_ == immutable_.get()) {
    // immutable iterator is advanced -- update prev_key_
    prev_key_ = key().ToString();
    is_prev_inclusive_ = false;
    is_prev_set_ = true;
  }

  current_->Next();
  UpdateCurrent();
}

Slice TailingIterator::key() const {
  assert(Valid());
  return current_->key();
}

Slice TailingIterator::value() const {
  assert(Valid());
  return current_->value();
}

Status TailingIterator::status() const {
  if (!status_.ok()) {
    return status_;
  } else if (!mutable_->status().ok()) {
    return mutable_->status();
  } else {
    return immutable_->status();
  }
}

void TailingIterator::Prev() {
  status_ = Status::NotSupported("This iterator doesn't support Prev()");
}

void TailingIterator::SeekToLast() {
  status_ = Status::NotSupported("This iterator doesn't support SeekToLast()");
}

void TailingIterator::Cleanup() {
  // Release old super version if necessary
  mutable_.reset();
  immutable_.reset();
  if (super_version_ != nullptr && super_version_->Unref()) {
    DBImpl::DeletionState deletion_state;
    db_->mutex_.Lock();
    super_version_->Cleanup();
    db_->FindObsoleteFiles(deletion_state, false, true);
    db_->mutex_.Unlock();
    delete super_version_;
    if (deletion_state.HaveSomethingToDelete()) {
      db_->PurgeObsoleteFiles(deletion_state);
    }
  }
}

void TailingIterator::CreateIterators() {
  Cleanup();
  super_version_= cfd_->GetReferencedSuperVersion(&(db_->mutex_));

  Iterator* mutable_iter = super_version_->mem->NewIterator(read_options_);
  // create a DBIter that only uses memtable content; see NewIterator()
  mutable_.reset(
      NewDBIterator(env_, *cfd_->options(), cfd_->user_comparator(),
                    mutable_iter, kMaxSequenceNumber));

  std::vector<Iterator*> list;
  super_version_->imm->AddIterators(read_options_, &list);
  super_version_->current->AddIterators(
      read_options_, *cfd_->soptions(), &list);
  Iterator* immutable_iter =
      NewMergingIterator(&cfd_->internal_comparator(), &list[0], list.size());

  // create a DBIter that only uses memtable content; see NewIterator()
  immutable_.reset(
      NewDBIterator(env_, *cfd_->options(), cfd_->user_comparator(),
                    immutable_iter, kMaxSequenceNumber));

  current_ = nullptr;
  is_prev_set_ = false;
}

void TailingIterator::UpdateCurrent() {
  current_ = nullptr;

  if (mutable_->Valid()) {
    current_ = mutable_.get();
  }
  const Comparator* cmp = cfd_->user_comparator();
  if (immutable_->Valid() &&
      (current_ == nullptr ||
       cmp->Compare(immutable_->key(), current_->key()) < 0)) {
    current_ = immutable_.get();
  }

  if (!status_.ok()) {
    // reset status that was set by Prev() or SeekToLast()
    status_ = Status::OK();
  }
}

bool TailingIterator::IsCurrentVersion() const {
  return super_version_ != nullptr &&
         super_version_->version_number == cfd_->GetSuperVersionNumber();
}

bool TailingIterator::IsSamePrefix(const Slice& target) const {
  const SliceTransform* extractor = cfd_->options()->prefix_extractor.get();

  assert(extractor);
  assert(is_prev_set_);

  return extractor->Transform(target)
    .compare(extractor->Transform(prev_key_)) == 0;
}

void TailingIterator::SeekImmutable(const Slice& target) {
  prev_key_ = target.ToString();
  is_prev_inclusive_ = true;
  is_prev_set_ = true;

  immutable_->Seek(target);
}

}  // namespace rocksdb
#endif  // ROCKSDB_LITE
