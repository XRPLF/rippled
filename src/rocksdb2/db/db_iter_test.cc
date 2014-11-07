//  Copyright (c) 2014, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.

#include <string>
#include <vector>
#include <algorithm>
#include <utility>

#include "db/dbformat.h"
#include "rocksdb/comparator.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"
#include "rocksdb/statistics.h"
#include "db/db_iter.h"
#include "util/testharness.h"
#include "utilities/merge_operators.h"

namespace rocksdb {

static uint32_t TestGetTickerCount(const Options& options,
                                   Tickers ticker_type) {
  return options.statistics->getTickerCount(ticker_type);
}

class TestIterator : public Iterator {
 public:
  explicit TestIterator(const Comparator* comparator)
      : initialized_(false),
        valid_(false),
        sequence_number_(0),
        iter_(0),
        cmp(comparator) {}

  void AddMerge(std::string key, std::string value) {
    Add(key, kTypeMerge, value);
  }

  void AddDeletion(std::string key) { Add(key, kTypeDeletion, std::string()); }

  void AddPut(std::string key, std::string value) {
    Add(key, kTypeValue, value);
  }

  void Add(std::string key, ValueType type, std::string value) {
    valid_ = true;
    ParsedInternalKey internal_key(key, sequence_number_++, type);
    data_.push_back(std::pair<std::string, std::string>(std::string(), value));
    AppendInternalKey(&data_.back().first, internal_key);
  }

  // should be called before operations with iterator
  void Finish() {
    initialized_ = true;
    std::sort(data_.begin(), data_.end(),
              [this](std::pair<std::string, std::string> a,
                     std::pair<std::string, std::string> b) {
      return (cmp.Compare(a.first, b.first) < 0);
    });
  }

  virtual bool Valid() const override {
    assert(initialized_);
    return valid_;
  }

  virtual void SeekToFirst() override {
    assert(initialized_);
    valid_ = (data_.size() > 0);
    iter_ = 0;
  }

  virtual void SeekToLast() override {
    assert(initialized_);
    valid_ = (data_.size() > 0);
    iter_ = data_.size() - 1;
  }

  virtual void Seek(const Slice& target) override {
    assert(initialized_);
    SeekToFirst();
    if (!valid_) {
      return;
    }
    while (iter_ < data_.size() &&
           (cmp.Compare(data_[iter_].first, target) < 0)) {
      ++iter_;
    }

    if (iter_ == data_.size()) {
      valid_ = false;
    }
  }

  virtual void Next() override {
    assert(initialized_);
    if (data_.empty() || (iter_ == data_.size() - 1)) {
      valid_ = false;
    } else {
      ++iter_;
    }
  }

  virtual void Prev() override {
    assert(initialized_);
    if (iter_ == 0) {
      valid_ = false;
    } else {
      --iter_;
    }
  }

  virtual Slice key() const override {
    assert(initialized_);
    return data_[iter_].first;
  }

  virtual Slice value() const override {
    assert(initialized_);
    return data_[iter_].second;
  }

  virtual Status status() const override {
    assert(initialized_);
    return Status::OK();
  }

 private:
  bool initialized_;
  bool valid_;
  size_t sequence_number_;
  size_t iter_;

  InternalKeyComparator cmp;
  std::vector<std::pair<std::string, std::string>> data_;
};

class DBIteratorTest {
 public:
  Env* env_;

  DBIteratorTest() : env_(Env::Default()) {}
};

TEST(DBIteratorTest, DBIteratorPrevNext) {
  Options options;

  {
    TestIterator* internal_iter = new TestIterator(BytewiseComparator());
    internal_iter->AddDeletion("a");
    internal_iter->AddDeletion("a");
    internal_iter->AddDeletion("a");
    internal_iter->AddDeletion("a");
    internal_iter->AddPut("a", "val_a");

    internal_iter->AddPut("b", "val_b");
    internal_iter->Finish();

    std::unique_ptr<Iterator> db_iter(
        NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 10));

    db_iter->SeekToLast();
    ASSERT_TRUE(db_iter->Valid());
    ASSERT_EQ(db_iter->key().ToString(), "b");
    ASSERT_EQ(db_iter->value().ToString(), "val_b");

    db_iter->Prev();
    ASSERT_TRUE(db_iter->Valid());
    ASSERT_EQ(db_iter->key().ToString(), "a");
    ASSERT_EQ(db_iter->value().ToString(), "val_a");

    db_iter->Next();
    ASSERT_TRUE(db_iter->Valid());
    ASSERT_EQ(db_iter->key().ToString(), "b");
    ASSERT_EQ(db_iter->value().ToString(), "val_b");

    db_iter->Next();
    ASSERT_TRUE(!db_iter->Valid());
  }

  {
    TestIterator* internal_iter = new TestIterator(BytewiseComparator());
    internal_iter->AddDeletion("a");
    internal_iter->AddDeletion("a");
    internal_iter->AddDeletion("a");
    internal_iter->AddDeletion("a");
    internal_iter->AddPut("a", "val_a");

    internal_iter->AddPut("b", "val_b");
    internal_iter->Finish();

    std::unique_ptr<Iterator> db_iter(
        NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 10));

    db_iter->SeekToFirst();
    ASSERT_TRUE(db_iter->Valid());
    ASSERT_EQ(db_iter->key().ToString(), "a");
    ASSERT_EQ(db_iter->value().ToString(), "val_a");

    db_iter->Next();
    ASSERT_TRUE(db_iter->Valid());
    ASSERT_EQ(db_iter->key().ToString(), "b");
    ASSERT_EQ(db_iter->value().ToString(), "val_b");

    db_iter->Prev();
    ASSERT_TRUE(db_iter->Valid());
    ASSERT_EQ(db_iter->key().ToString(), "a");
    ASSERT_EQ(db_iter->value().ToString(), "val_a");

    db_iter->Prev();
    ASSERT_TRUE(!db_iter->Valid());
  }

  {
    Options options;
    TestIterator* internal_iter = new TestIterator(BytewiseComparator());
    internal_iter->AddPut("a", "val_a");
    internal_iter->AddPut("b", "val_b");

    internal_iter->AddPut("a", "val_a");
    internal_iter->AddPut("b", "val_b");

    internal_iter->AddPut("a", "val_a");
    internal_iter->AddPut("b", "val_b");

    internal_iter->AddPut("a", "val_a");
    internal_iter->AddPut("b", "val_b");

    internal_iter->AddPut("a", "val_a");
    internal_iter->AddPut("b", "val_b");
    internal_iter->Finish();

    std::unique_ptr<Iterator> db_iter(
        NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 2));
    db_iter->SeekToLast();
    ASSERT_TRUE(db_iter->Valid());
    ASSERT_EQ(db_iter->key().ToString(), "b");
    ASSERT_EQ(db_iter->value().ToString(), "val_b");

    db_iter->Next();
    ASSERT_TRUE(!db_iter->Valid());

    db_iter->SeekToLast();
    ASSERT_TRUE(db_iter->Valid());
    ASSERT_EQ(db_iter->key().ToString(), "b");
    ASSERT_EQ(db_iter->value().ToString(), "val_b");
  }

  {
    Options options;
    TestIterator* internal_iter = new TestIterator(BytewiseComparator());
    internal_iter->AddPut("a", "val_a");
    internal_iter->AddPut("a", "val_a");
    internal_iter->AddPut("a", "val_a");
    internal_iter->AddPut("a", "val_a");
    internal_iter->AddPut("a", "val_a");

    internal_iter->AddPut("b", "val_b");

    internal_iter->AddPut("c", "val_c");
    internal_iter->Finish();

    std::unique_ptr<Iterator> db_iter(
        NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 10));
    db_iter->SeekToLast();
    ASSERT_TRUE(db_iter->Valid());
    ASSERT_EQ(db_iter->key().ToString(), "c");
    ASSERT_EQ(db_iter->value().ToString(), "val_c");

    db_iter->Prev();
    ASSERT_TRUE(db_iter->Valid());
    ASSERT_EQ(db_iter->key().ToString(), "b");
    ASSERT_EQ(db_iter->value().ToString(), "val_b");

    db_iter->Next();
    ASSERT_TRUE(db_iter->Valid());
    ASSERT_EQ(db_iter->key().ToString(), "c");
    ASSERT_EQ(db_iter->value().ToString(), "val_c");
  }
}

TEST(DBIteratorTest, DBIteratorEmpty) {
  Options options;

  {
    TestIterator* internal_iter = new TestIterator(BytewiseComparator());
    internal_iter->Finish();

    std::unique_ptr<Iterator> db_iter(
        NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 0));
    db_iter->SeekToLast();
    ASSERT_TRUE(!db_iter->Valid());
  }

  {
    TestIterator* internal_iter = new TestIterator(BytewiseComparator());
    internal_iter->Finish();

    std::unique_ptr<Iterator> db_iter(
        NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 0));
    db_iter->SeekToFirst();
    ASSERT_TRUE(!db_iter->Valid());
  }
}

TEST(DBIteratorTest, DBIteratorUseSkipCountSkips) {
  Options options;
  options.statistics = rocksdb::CreateDBStatistics();
  options.merge_operator = MergeOperators::CreateFromStringId("stringappend");

  TestIterator* internal_iter = new TestIterator(BytewiseComparator());
  for (size_t i = 0; i < 200; ++i) {
    internal_iter->AddPut("a", "a");
    internal_iter->AddPut("b", "b");
    internal_iter->AddPut("c", "c");
  }
  internal_iter->Finish();

  std::unique_ptr<Iterator> db_iter(
      NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 2));
  db_iter->SeekToLast();
  ASSERT_TRUE(db_iter->Valid());
  ASSERT_EQ(db_iter->key().ToString(), "c");
  ASSERT_EQ(db_iter->value().ToString(), "c");
  ASSERT_EQ(TestGetTickerCount(options, NUMBER_OF_RESEEKS_IN_ITERATION), 1u);

  db_iter->Prev();
  ASSERT_TRUE(db_iter->Valid());
  ASSERT_EQ(db_iter->key().ToString(), "b");
  ASSERT_EQ(db_iter->value().ToString(), "b");
  ASSERT_EQ(TestGetTickerCount(options, NUMBER_OF_RESEEKS_IN_ITERATION), 2u);

  db_iter->Prev();
  ASSERT_TRUE(db_iter->Valid());
  ASSERT_EQ(db_iter->key().ToString(), "a");
  ASSERT_EQ(db_iter->value().ToString(), "a");
  ASSERT_EQ(TestGetTickerCount(options, NUMBER_OF_RESEEKS_IN_ITERATION), 3u);

  db_iter->Prev();
  ASSERT_TRUE(!db_iter->Valid());
  ASSERT_EQ(TestGetTickerCount(options, NUMBER_OF_RESEEKS_IN_ITERATION), 3u);
}

TEST(DBIteratorTest, DBIteratorUseSkip) {
  Options options;
  options.merge_operator = MergeOperators::CreateFromStringId("stringappend");
  {
    for (size_t i = 0; i < 200; ++i) {
      TestIterator* internal_iter = new TestIterator(BytewiseComparator());
      internal_iter->AddMerge("b", "merge_1");
      internal_iter->AddMerge("a", "merge_2");
      for (size_t i = 0; i < 200; ++i) {
        internal_iter->AddPut("c", std::to_string(i));
      }
      internal_iter->Finish();

      options.statistics = rocksdb::CreateDBStatistics();
      std::unique_ptr<Iterator> db_iter(NewDBIterator(
          env_, options, BytewiseComparator(), internal_iter, i + 2));
      db_iter->SeekToLast();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_EQ(db_iter->key().ToString(), "c");
      ASSERT_EQ(db_iter->value().ToString(), std::to_string(i));
      db_iter->Prev();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_EQ(db_iter->key().ToString(), "b");
      ASSERT_EQ(db_iter->value().ToString(), "merge_1");
      db_iter->Prev();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_EQ(db_iter->key().ToString(), "a");
      ASSERT_EQ(db_iter->value().ToString(), "merge_2");
      db_iter->Prev();

      ASSERT_TRUE(!db_iter->Valid());
    }
  }

  {
    for (size_t i = 0; i < 200; ++i) {
      TestIterator* internal_iter = new TestIterator(BytewiseComparator());
      internal_iter->AddMerge("b", "merge_1");
      internal_iter->AddMerge("a", "merge_2");
      for (size_t i = 0; i < 200; ++i) {
        internal_iter->AddDeletion("c");
      }
      internal_iter->AddPut("c", "200");
      internal_iter->Finish();

      std::unique_ptr<Iterator> db_iter(NewDBIterator(
          env_, options, BytewiseComparator(), internal_iter, i + 2));
      db_iter->SeekToLast();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_EQ(db_iter->key().ToString(), "b");
      ASSERT_EQ(db_iter->value().ToString(), "merge_1");
      db_iter->Prev();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_EQ(db_iter->key().ToString(), "a");
      ASSERT_EQ(db_iter->value().ToString(), "merge_2");
      db_iter->Prev();

      ASSERT_TRUE(!db_iter->Valid());
    }

    {
      TestIterator* internal_iter = new TestIterator(BytewiseComparator());
      internal_iter->AddMerge("b", "merge_1");
      internal_iter->AddMerge("a", "merge_2");
      for (size_t i = 0; i < 200; ++i) {
        internal_iter->AddDeletion("c");
      }
      internal_iter->AddPut("c", "200");
      internal_iter->Finish();

      std::unique_ptr<Iterator> db_iter(NewDBIterator(
          env_, options, BytewiseComparator(), internal_iter, 202));
      db_iter->SeekToLast();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_EQ(db_iter->key().ToString(), "c");
      ASSERT_EQ(db_iter->value().ToString(), "200");
      db_iter->Prev();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_EQ(db_iter->key().ToString(), "b");
      ASSERT_EQ(db_iter->value().ToString(), "merge_1");
      db_iter->Prev();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_EQ(db_iter->key().ToString(), "a");
      ASSERT_EQ(db_iter->value().ToString(), "merge_2");
      db_iter->Prev();

      ASSERT_TRUE(!db_iter->Valid());
    }
  }

  {
    for (size_t i = 0; i < 200; ++i) {
      TestIterator* internal_iter = new TestIterator(BytewiseComparator());
      for (size_t i = 0; i < 200; ++i) {
        internal_iter->AddDeletion("c");
      }
      internal_iter->AddPut("c", "200");
      internal_iter->Finish();
      std::unique_ptr<Iterator> db_iter(
          NewDBIterator(env_, options, BytewiseComparator(), internal_iter, i));
      db_iter->SeekToLast();
      ASSERT_TRUE(!db_iter->Valid());

      db_iter->SeekToFirst();
      ASSERT_TRUE(!db_iter->Valid());
    }

    TestIterator* internal_iter = new TestIterator(BytewiseComparator());
    for (size_t i = 0; i < 200; ++i) {
      internal_iter->AddDeletion("c");
    }
    internal_iter->AddPut("c", "200");
    internal_iter->Finish();
    std::unique_ptr<Iterator> db_iter(
        NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 200));
    db_iter->SeekToLast();
    ASSERT_TRUE(db_iter->Valid());
    ASSERT_EQ(db_iter->key().ToString(), "c");
    ASSERT_EQ(db_iter->value().ToString(), "200");

    db_iter->Prev();
    ASSERT_TRUE(!db_iter->Valid());

    db_iter->SeekToFirst();
    ASSERT_TRUE(db_iter->Valid());
    ASSERT_EQ(db_iter->key().ToString(), "c");
    ASSERT_EQ(db_iter->value().ToString(), "200");

    db_iter->Next();
    ASSERT_TRUE(!db_iter->Valid());
  }

  {
    for (size_t i = 0; i < 200; ++i) {
      TestIterator* internal_iter = new TestIterator(BytewiseComparator());
      internal_iter->AddMerge("b", "merge_1");
      internal_iter->AddMerge("a", "merge_2");
      for (size_t i = 0; i < 200; ++i) {
        internal_iter->AddPut("d", std::to_string(i));
      }

      for (size_t i = 0; i < 200; ++i) {
        internal_iter->AddPut("c", std::to_string(i));
      }
      internal_iter->Finish();

      std::unique_ptr<Iterator> db_iter(NewDBIterator(
          env_, options, BytewiseComparator(), internal_iter, i + 2));
      db_iter->SeekToLast();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_EQ(db_iter->key().ToString(), "d");
      ASSERT_EQ(db_iter->value().ToString(), std::to_string(i));
      db_iter->Prev();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_EQ(db_iter->key().ToString(), "b");
      ASSERT_EQ(db_iter->value().ToString(), "merge_1");
      db_iter->Prev();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_EQ(db_iter->key().ToString(), "a");
      ASSERT_EQ(db_iter->value().ToString(), "merge_2");
      db_iter->Prev();

      ASSERT_TRUE(!db_iter->Valid());
    }
  }

  {
    for (size_t i = 0; i < 200; ++i) {
      TestIterator* internal_iter = new TestIterator(BytewiseComparator());
      internal_iter->AddMerge("b", "b");
      internal_iter->AddMerge("a", "a");
      for (size_t i = 0; i < 200; ++i) {
        internal_iter->AddMerge("c", std::to_string(i));
      }
      internal_iter->Finish();

      std::unique_ptr<Iterator> db_iter(NewDBIterator(
          env_, options, BytewiseComparator(), internal_iter, i + 2));
      db_iter->SeekToLast();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_EQ(db_iter->key().ToString(), "c");
      std::string merge_result = "0";
      for (size_t j = 1; j <= i; ++j) {
        merge_result += "," + std::to_string(j);
      }
      ASSERT_EQ(db_iter->value().ToString(), merge_result);

      db_iter->Prev();
      ASSERT_TRUE(db_iter->Valid());
      ASSERT_EQ(db_iter->key().ToString(), "b");
      ASSERT_EQ(db_iter->value().ToString(), "b");

      db_iter->Prev();
      ASSERT_TRUE(db_iter->Valid());
      ASSERT_EQ(db_iter->key().ToString(), "a");
      ASSERT_EQ(db_iter->value().ToString(), "a");

      db_iter->Prev();
      ASSERT_TRUE(!db_iter->Valid());
    }
  }
}

TEST(DBIteratorTest, DBIterator) {
  Options options;
  options.merge_operator = MergeOperators::CreateFromStringId("stringappend");
  {
    TestIterator* internal_iter = new TestIterator(BytewiseComparator());
    internal_iter->AddPut("a", "0");
    internal_iter->AddPut("b", "0");
    internal_iter->AddDeletion("b");
    internal_iter->AddMerge("a", "1");
    internal_iter->AddMerge("b", "2");
    internal_iter->Finish();

    std::unique_ptr<Iterator> db_iter(
        NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 1));
    db_iter->SeekToFirst();
    ASSERT_TRUE(db_iter->Valid());
    ASSERT_EQ(db_iter->key().ToString(), "a");
    ASSERT_EQ(db_iter->value().ToString(), "0");
    db_iter->Next();
    ASSERT_TRUE(db_iter->Valid());
    ASSERT_EQ(db_iter->key().ToString(), "b");
  }

  {
    TestIterator* internal_iter = new TestIterator(BytewiseComparator());
    internal_iter->AddPut("a", "0");
    internal_iter->AddPut("b", "0");
    internal_iter->AddDeletion("b");
    internal_iter->AddMerge("a", "1");
    internal_iter->AddMerge("b", "2");
    internal_iter->Finish();

    std::unique_ptr<Iterator> db_iter(
        NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 0));
    db_iter->SeekToFirst();
    ASSERT_TRUE(db_iter->Valid());
    ASSERT_EQ(db_iter->key().ToString(), "a");
    ASSERT_EQ(db_iter->value().ToString(), "0");
    db_iter->Next();
    ASSERT_TRUE(!db_iter->Valid());
  }

  {
    TestIterator* internal_iter = new TestIterator(BytewiseComparator());
    internal_iter->AddPut("a", "0");
    internal_iter->AddPut("b", "0");
    internal_iter->AddDeletion("b");
    internal_iter->AddMerge("a", "1");
    internal_iter->AddMerge("b", "2");
    internal_iter->Finish();

    std::unique_ptr<Iterator> db_iter(
        NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 2));
    db_iter->SeekToFirst();
    ASSERT_TRUE(db_iter->Valid());
    ASSERT_EQ(db_iter->key().ToString(), "a");
    ASSERT_EQ(db_iter->value().ToString(), "0");
    db_iter->Next();
    ASSERT_TRUE(!db_iter->Valid());
  }

  {
    TestIterator* internal_iter = new TestIterator(BytewiseComparator());
    internal_iter->AddPut("a", "0");
    internal_iter->AddPut("b", "0");
    internal_iter->AddDeletion("b");
    internal_iter->AddMerge("a", "1");
    internal_iter->AddMerge("b", "2");
    internal_iter->Finish();

    std::unique_ptr<Iterator> db_iter(
        NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 4));
    db_iter->SeekToFirst();
    ASSERT_TRUE(db_iter->Valid());
    ASSERT_EQ(db_iter->key().ToString(), "a");
    ASSERT_EQ(db_iter->value().ToString(), "0,1");
    db_iter->Next();
    ASSERT_TRUE(db_iter->Valid());
    ASSERT_EQ(db_iter->key().ToString(), "b");
    ASSERT_EQ(db_iter->value().ToString(), "2");
    db_iter->Next();
    ASSERT_TRUE(!db_iter->Valid());
  }

  {
    {
      TestIterator* internal_iter = new TestIterator(BytewiseComparator());
      internal_iter->AddMerge("a", "merge_1");
      internal_iter->AddMerge("a", "merge_2");
      internal_iter->AddMerge("a", "merge_3");
      internal_iter->AddPut("a", "put_1");
      internal_iter->AddMerge("a", "merge_4");
      internal_iter->AddMerge("a", "merge_5");
      internal_iter->AddMerge("a", "merge_6");
      internal_iter->Finish();

      std::unique_ptr<Iterator> db_iter(
          NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 0));
      db_iter->SeekToLast();
      ASSERT_TRUE(db_iter->Valid());
      ASSERT_EQ(db_iter->key().ToString(), "a");
      ASSERT_EQ(db_iter->value().ToString(), "merge_1");
      db_iter->Prev();
      ASSERT_TRUE(!db_iter->Valid());
    }

    {
      TestIterator* internal_iter = new TestIterator(BytewiseComparator());
      internal_iter->AddMerge("a", "merge_1");
      internal_iter->AddMerge("a", "merge_2");
      internal_iter->AddMerge("a", "merge_3");
      internal_iter->AddPut("a", "put_1");
      internal_iter->AddMerge("a", "merge_4");
      internal_iter->AddMerge("a", "merge_5");
      internal_iter->AddMerge("a", "merge_6");
      internal_iter->Finish();

      std::unique_ptr<Iterator> db_iter(
          NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 1));
      db_iter->SeekToLast();
      ASSERT_TRUE(db_iter->Valid());
      ASSERT_EQ(db_iter->key().ToString(), "a");
      ASSERT_EQ(db_iter->value().ToString(), "merge_1,merge_2");
      db_iter->Prev();
      ASSERT_TRUE(!db_iter->Valid());
    }

    {
      TestIterator* internal_iter = new TestIterator(BytewiseComparator());
      internal_iter->AddMerge("a", "merge_1");
      internal_iter->AddMerge("a", "merge_2");
      internal_iter->AddMerge("a", "merge_3");
      internal_iter->AddPut("a", "put_1");
      internal_iter->AddMerge("a", "merge_4");
      internal_iter->AddMerge("a", "merge_5");
      internal_iter->AddMerge("a", "merge_6");
      internal_iter->Finish();

      std::unique_ptr<Iterator> db_iter(
          NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 2));
      db_iter->SeekToLast();
      ASSERT_TRUE(db_iter->Valid());
      ASSERT_EQ(db_iter->key().ToString(), "a");
      ASSERT_EQ(db_iter->value().ToString(), "merge_1,merge_2,merge_3");
      db_iter->Prev();
      ASSERT_TRUE(!db_iter->Valid());
    }

    {
      TestIterator* internal_iter = new TestIterator(BytewiseComparator());
      internal_iter->AddMerge("a", "merge_1");
      internal_iter->AddMerge("a", "merge_2");
      internal_iter->AddMerge("a", "merge_3");
      internal_iter->AddPut("a", "put_1");
      internal_iter->AddMerge("a", "merge_4");
      internal_iter->AddMerge("a", "merge_5");
      internal_iter->AddMerge("a", "merge_6");
      internal_iter->Finish();

      std::unique_ptr<Iterator> db_iter(
          NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 3));
      db_iter->SeekToLast();
      ASSERT_TRUE(db_iter->Valid());
      ASSERT_EQ(db_iter->key().ToString(), "a");
      ASSERT_EQ(db_iter->value().ToString(), "put_1");
      db_iter->Prev();
      ASSERT_TRUE(!db_iter->Valid());
    }

    {
      TestIterator* internal_iter = new TestIterator(BytewiseComparator());
      internal_iter->AddMerge("a", "merge_1");
      internal_iter->AddMerge("a", "merge_2");
      internal_iter->AddMerge("a", "merge_3");
      internal_iter->AddPut("a", "put_1");
      internal_iter->AddMerge("a", "merge_4");
      internal_iter->AddMerge("a", "merge_5");
      internal_iter->AddMerge("a", "merge_6");
      internal_iter->Finish();

      std::unique_ptr<Iterator> db_iter(
          NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 4));
      db_iter->SeekToLast();
      ASSERT_TRUE(db_iter->Valid());
      ASSERT_EQ(db_iter->key().ToString(), "a");
      ASSERT_EQ(db_iter->value().ToString(), "put_1,merge_4");
      db_iter->Prev();
      ASSERT_TRUE(!db_iter->Valid());
    }

    {
      TestIterator* internal_iter = new TestIterator(BytewiseComparator());
      internal_iter->AddMerge("a", "merge_1");
      internal_iter->AddMerge("a", "merge_2");
      internal_iter->AddMerge("a", "merge_3");
      internal_iter->AddPut("a", "put_1");
      internal_iter->AddMerge("a", "merge_4");
      internal_iter->AddMerge("a", "merge_5");
      internal_iter->AddMerge("a", "merge_6");
      internal_iter->Finish();

      std::unique_ptr<Iterator> db_iter(
          NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 5));
      db_iter->SeekToLast();
      ASSERT_TRUE(db_iter->Valid());
      ASSERT_EQ(db_iter->key().ToString(), "a");
      ASSERT_EQ(db_iter->value().ToString(), "put_1,merge_4,merge_5");
      db_iter->Prev();
      ASSERT_TRUE(!db_iter->Valid());
    }

    {
      TestIterator* internal_iter = new TestIterator(BytewiseComparator());
      internal_iter->AddMerge("a", "merge_1");
      internal_iter->AddMerge("a", "merge_2");
      internal_iter->AddMerge("a", "merge_3");
      internal_iter->AddPut("a", "put_1");
      internal_iter->AddMerge("a", "merge_4");
      internal_iter->AddMerge("a", "merge_5");
      internal_iter->AddMerge("a", "merge_6");
      internal_iter->Finish();

      std::unique_ptr<Iterator> db_iter(
          NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 6));
      db_iter->SeekToLast();
      ASSERT_TRUE(db_iter->Valid());
      ASSERT_EQ(db_iter->key().ToString(), "a");
      ASSERT_EQ(db_iter->value().ToString(), "put_1,merge_4,merge_5,merge_6");
      db_iter->Prev();
      ASSERT_TRUE(!db_iter->Valid());
    }
  }

  {
    {
      TestIterator* internal_iter = new TestIterator(BytewiseComparator());
      internal_iter->AddMerge("a", "merge_1");
      internal_iter->AddMerge("a", "merge_2");
      internal_iter->AddMerge("a", "merge_3");
      internal_iter->AddDeletion("a");
      internal_iter->AddMerge("a", "merge_4");
      internal_iter->AddMerge("a", "merge_5");
      internal_iter->AddMerge("a", "merge_6");
      internal_iter->Finish();

      std::unique_ptr<Iterator> db_iter(
          NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 0));
      db_iter->SeekToLast();
      ASSERT_TRUE(db_iter->Valid());
      ASSERT_EQ(db_iter->key().ToString(), "a");
      ASSERT_EQ(db_iter->value().ToString(), "merge_1");
      db_iter->Prev();
      ASSERT_TRUE(!db_iter->Valid());
    }

    {
      TestIterator* internal_iter = new TestIterator(BytewiseComparator());
      internal_iter->AddMerge("a", "merge_1");
      internal_iter->AddMerge("a", "merge_2");
      internal_iter->AddMerge("a", "merge_3");
      internal_iter->AddDeletion("a");
      internal_iter->AddMerge("a", "merge_4");
      internal_iter->AddMerge("a", "merge_5");
      internal_iter->AddMerge("a", "merge_6");
      internal_iter->Finish();

      std::unique_ptr<Iterator> db_iter(
          NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 1));
      db_iter->SeekToLast();
      ASSERT_TRUE(db_iter->Valid());
      ASSERT_EQ(db_iter->key().ToString(), "a");
      ASSERT_EQ(db_iter->value().ToString(), "merge_1,merge_2");
      db_iter->Prev();
      ASSERT_TRUE(!db_iter->Valid());
    }

    {
      TestIterator* internal_iter = new TestIterator(BytewiseComparator());
      internal_iter->AddMerge("a", "merge_1");
      internal_iter->AddMerge("a", "merge_2");
      internal_iter->AddMerge("a", "merge_3");
      internal_iter->AddDeletion("a");
      internal_iter->AddMerge("a", "merge_4");
      internal_iter->AddMerge("a", "merge_5");
      internal_iter->AddMerge("a", "merge_6");
      internal_iter->Finish();

      std::unique_ptr<Iterator> db_iter(
          NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 2));
      db_iter->SeekToLast();
      ASSERT_TRUE(db_iter->Valid());
      ASSERT_EQ(db_iter->key().ToString(), "a");
      ASSERT_EQ(db_iter->value().ToString(), "merge_1,merge_2,merge_3");
      db_iter->Prev();
      ASSERT_TRUE(!db_iter->Valid());
    }

    {
      TestIterator* internal_iter = new TestIterator(BytewiseComparator());
      internal_iter->AddMerge("a", "merge_1");
      internal_iter->AddMerge("a", "merge_2");
      internal_iter->AddMerge("a", "merge_3");
      internal_iter->AddDeletion("a");
      internal_iter->AddMerge("a", "merge_4");
      internal_iter->AddMerge("a", "merge_5");
      internal_iter->AddMerge("a", "merge_6");
      internal_iter->Finish();

      std::unique_ptr<Iterator> db_iter(
          NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 3));
      db_iter->SeekToLast();
      ASSERT_TRUE(!db_iter->Valid());
    }

    {
      TestIterator* internal_iter = new TestIterator(BytewiseComparator());
      internal_iter->AddMerge("a", "merge_1");
      internal_iter->AddMerge("a", "merge_2");
      internal_iter->AddMerge("a", "merge_3");
      internal_iter->AddDeletion("a");
      internal_iter->AddMerge("a", "merge_4");
      internal_iter->AddMerge("a", "merge_5");
      internal_iter->AddMerge("a", "merge_6");
      internal_iter->Finish();

      std::unique_ptr<Iterator> db_iter(
          NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 4));
      db_iter->SeekToLast();
      ASSERT_TRUE(db_iter->Valid());
      ASSERT_EQ(db_iter->key().ToString(), "a");
      ASSERT_EQ(db_iter->value().ToString(), "merge_4");
      db_iter->Prev();
      ASSERT_TRUE(!db_iter->Valid());
    }

    {
      TestIterator* internal_iter = new TestIterator(BytewiseComparator());
      internal_iter->AddMerge("a", "merge_1");
      internal_iter->AddMerge("a", "merge_2");
      internal_iter->AddMerge("a", "merge_3");
      internal_iter->AddDeletion("a");
      internal_iter->AddMerge("a", "merge_4");
      internal_iter->AddMerge("a", "merge_5");
      internal_iter->AddMerge("a", "merge_6");
      internal_iter->Finish();

      std::unique_ptr<Iterator> db_iter(
          NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 5));
      db_iter->SeekToLast();
      ASSERT_TRUE(db_iter->Valid());
      ASSERT_EQ(db_iter->key().ToString(), "a");
      ASSERT_EQ(db_iter->value().ToString(), "merge_4,merge_5");
      db_iter->Prev();
      ASSERT_TRUE(!db_iter->Valid());
    }

    {
      TestIterator* internal_iter = new TestIterator(BytewiseComparator());
      internal_iter->AddMerge("a", "merge_1");
      internal_iter->AddMerge("a", "merge_2");
      internal_iter->AddMerge("a", "merge_3");
      internal_iter->AddDeletion("a");
      internal_iter->AddMerge("a", "merge_4");
      internal_iter->AddMerge("a", "merge_5");
      internal_iter->AddMerge("a", "merge_6");
      internal_iter->Finish();

      std::unique_ptr<Iterator> db_iter(
          NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 6));
      db_iter->SeekToLast();
      ASSERT_TRUE(db_iter->Valid());
      ASSERT_EQ(db_iter->key().ToString(), "a");
      ASSERT_EQ(db_iter->value().ToString(), "merge_4,merge_5,merge_6");
      db_iter->Prev();
      ASSERT_TRUE(!db_iter->Valid());
    }
  }

  {
    {
      TestIterator* internal_iter = new TestIterator(BytewiseComparator());
      internal_iter->AddMerge("a", "merge_1");
      internal_iter->AddPut("b", "val");
      internal_iter->AddMerge("b", "merge_2");

      internal_iter->AddDeletion("b");
      internal_iter->AddMerge("b", "merge_3");

      internal_iter->AddMerge("c", "merge_4");
      internal_iter->AddMerge("c", "merge_5");

      internal_iter->AddDeletion("b");
      internal_iter->AddMerge("b", "merge_6");
      internal_iter->AddMerge("b", "merge_7");
      internal_iter->AddMerge("b", "merge_8");
      internal_iter->AddMerge("b", "merge_9");
      internal_iter->AddMerge("b", "merge_10");
      internal_iter->AddMerge("b", "merge_11");

      internal_iter->AddDeletion("c");
      internal_iter->Finish();

      std::unique_ptr<Iterator> db_iter(
          NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 0));
      db_iter->SeekToLast();
      ASSERT_TRUE(db_iter->Valid());
      ASSERT_EQ(db_iter->key().ToString(), "a");
      ASSERT_EQ(db_iter->value().ToString(), "merge_1");
      db_iter->Prev();
      ASSERT_TRUE(!db_iter->Valid());
    }

    {
      TestIterator* internal_iter = new TestIterator(BytewiseComparator());
      internal_iter->AddMerge("a", "merge_1");
      internal_iter->AddPut("b", "val");
      internal_iter->AddMerge("b", "merge_2");

      internal_iter->AddDeletion("b");
      internal_iter->AddMerge("b", "merge_3");

      internal_iter->AddMerge("c", "merge_4");
      internal_iter->AddMerge("c", "merge_5");

      internal_iter->AddDeletion("b");
      internal_iter->AddMerge("b", "merge_6");
      internal_iter->AddMerge("b", "merge_7");
      internal_iter->AddMerge("b", "merge_8");
      internal_iter->AddMerge("b", "merge_9");
      internal_iter->AddMerge("b", "merge_10");
      internal_iter->AddMerge("b", "merge_11");

      internal_iter->AddDeletion("c");
      internal_iter->Finish();

      std::unique_ptr<Iterator> db_iter(
          NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 2));
      db_iter->SeekToLast();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_EQ(db_iter->key().ToString(), "b");
      ASSERT_EQ(db_iter->value().ToString(), "val,merge_2");
      db_iter->Prev();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_EQ(db_iter->key().ToString(), "a");
      ASSERT_EQ(db_iter->value().ToString(), "merge_1");
      db_iter->Prev();
      ASSERT_TRUE(!db_iter->Valid());
    }

    {
      TestIterator* internal_iter = new TestIterator(BytewiseComparator());
      internal_iter->AddMerge("a", "merge_1");
      internal_iter->AddPut("b", "val");
      internal_iter->AddMerge("b", "merge_2");

      internal_iter->AddDeletion("b");
      internal_iter->AddMerge("b", "merge_3");

      internal_iter->AddMerge("c", "merge_4");
      internal_iter->AddMerge("c", "merge_5");

      internal_iter->AddDeletion("b");
      internal_iter->AddMerge("b", "merge_6");
      internal_iter->AddMerge("b", "merge_7");
      internal_iter->AddMerge("b", "merge_8");
      internal_iter->AddMerge("b", "merge_9");
      internal_iter->AddMerge("b", "merge_10");
      internal_iter->AddMerge("b", "merge_11");

      internal_iter->AddDeletion("c");
      internal_iter->Finish();

      std::unique_ptr<Iterator> db_iter(
          NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 4));
      db_iter->SeekToLast();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_EQ(db_iter->key().ToString(), "b");
      ASSERT_EQ(db_iter->value().ToString(), "merge_3");
      db_iter->Prev();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_EQ(db_iter->key().ToString(), "a");
      ASSERT_EQ(db_iter->value().ToString(), "merge_1");
      db_iter->Prev();
      ASSERT_TRUE(!db_iter->Valid());
    }

    {
      TestIterator* internal_iter = new TestIterator(BytewiseComparator());
      internal_iter->AddMerge("a", "merge_1");
      internal_iter->AddPut("b", "val");
      internal_iter->AddMerge("b", "merge_2");

      internal_iter->AddDeletion("b");
      internal_iter->AddMerge("b", "merge_3");

      internal_iter->AddMerge("c", "merge_4");
      internal_iter->AddMerge("c", "merge_5");

      internal_iter->AddDeletion("b");
      internal_iter->AddMerge("b", "merge_6");
      internal_iter->AddMerge("b", "merge_7");
      internal_iter->AddMerge("b", "merge_8");
      internal_iter->AddMerge("b", "merge_9");
      internal_iter->AddMerge("b", "merge_10");
      internal_iter->AddMerge("b", "merge_11");

      internal_iter->AddDeletion("c");
      internal_iter->Finish();

      std::unique_ptr<Iterator> db_iter(
          NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 5));
      db_iter->SeekToLast();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_EQ(db_iter->key().ToString(), "c");
      ASSERT_EQ(db_iter->value().ToString(), "merge_4");
      db_iter->Prev();

      ASSERT_TRUE(db_iter->Valid());
      ASSERT_EQ(db_iter->key().ToString(), "b");
      ASSERT_EQ(db_iter->value().ToString(), "merge_3");
      db_iter->Prev();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_EQ(db_iter->key().ToString(), "a");
      ASSERT_EQ(db_iter->value().ToString(), "merge_1");
      db_iter->Prev();
      ASSERT_TRUE(!db_iter->Valid());
    }

    {
      TestIterator* internal_iter = new TestIterator(BytewiseComparator());
      internal_iter->AddMerge("a", "merge_1");
      internal_iter->AddPut("b", "val");
      internal_iter->AddMerge("b", "merge_2");

      internal_iter->AddDeletion("b");
      internal_iter->AddMerge("b", "merge_3");

      internal_iter->AddMerge("c", "merge_4");
      internal_iter->AddMerge("c", "merge_5");

      internal_iter->AddDeletion("b");
      internal_iter->AddMerge("b", "merge_6");
      internal_iter->AddMerge("b", "merge_7");
      internal_iter->AddMerge("b", "merge_8");
      internal_iter->AddMerge("b", "merge_9");
      internal_iter->AddMerge("b", "merge_10");
      internal_iter->AddMerge("b", "merge_11");

      internal_iter->AddDeletion("c");
      internal_iter->Finish();

      std::unique_ptr<Iterator> db_iter(
          NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 6));
      db_iter->SeekToLast();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_EQ(db_iter->key().ToString(), "c");
      ASSERT_EQ(db_iter->value().ToString(), "merge_4,merge_5");
      db_iter->Prev();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_TRUE(db_iter->Valid());
      ASSERT_EQ(db_iter->key().ToString(), "b");
      ASSERT_EQ(db_iter->value().ToString(), "merge_3");
      db_iter->Prev();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_EQ(db_iter->key().ToString(), "a");
      ASSERT_EQ(db_iter->value().ToString(), "merge_1");
      db_iter->Prev();
      ASSERT_TRUE(!db_iter->Valid());
    }

    {
      TestIterator* internal_iter = new TestIterator(BytewiseComparator());
      internal_iter->AddMerge("a", "merge_1");
      internal_iter->AddPut("b", "val");
      internal_iter->AddMerge("b", "merge_2");

      internal_iter->AddDeletion("b");
      internal_iter->AddMerge("b", "merge_3");

      internal_iter->AddMerge("c", "merge_4");
      internal_iter->AddMerge("c", "merge_5");

      internal_iter->AddDeletion("b");
      internal_iter->AddMerge("b", "merge_6");
      internal_iter->AddMerge("b", "merge_7");
      internal_iter->AddMerge("b", "merge_8");
      internal_iter->AddMerge("b", "merge_9");
      internal_iter->AddMerge("b", "merge_10");
      internal_iter->AddMerge("b", "merge_11");

      internal_iter->AddDeletion("c");
      internal_iter->Finish();

      std::unique_ptr<Iterator> db_iter(
          NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 7));
      db_iter->SeekToLast();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_EQ(db_iter->key().ToString(), "c");
      ASSERT_EQ(db_iter->value().ToString(), "merge_4,merge_5");
      db_iter->Prev();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_EQ(db_iter->key().ToString(), "a");
      ASSERT_EQ(db_iter->value().ToString(), "merge_1");
      db_iter->Prev();
      ASSERT_TRUE(!db_iter->Valid());
    }

    {
      TestIterator* internal_iter = new TestIterator(BytewiseComparator());
      internal_iter->AddMerge("a", "merge_1");
      internal_iter->AddPut("b", "val");
      internal_iter->AddMerge("b", "merge_2");

      internal_iter->AddDeletion("b");
      internal_iter->AddMerge("b", "merge_3");

      internal_iter->AddMerge("c", "merge_4");
      internal_iter->AddMerge("c", "merge_5");

      internal_iter->AddDeletion("b");
      internal_iter->AddMerge("b", "merge_6");
      internal_iter->AddMerge("b", "merge_7");
      internal_iter->AddMerge("b", "merge_8");
      internal_iter->AddMerge("b", "merge_9");
      internal_iter->AddMerge("b", "merge_10");
      internal_iter->AddMerge("b", "merge_11");

      internal_iter->AddDeletion("c");
      internal_iter->Finish();

      std::unique_ptr<Iterator> db_iter(
          NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 9));
      db_iter->SeekToLast();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_EQ(db_iter->key().ToString(), "c");
      ASSERT_EQ(db_iter->value().ToString(), "merge_4,merge_5");
      db_iter->Prev();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_TRUE(db_iter->Valid());
      ASSERT_EQ(db_iter->key().ToString(), "b");
      ASSERT_EQ(db_iter->value().ToString(), "merge_6,merge_7");
      db_iter->Prev();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_EQ(db_iter->key().ToString(), "a");
      ASSERT_EQ(db_iter->value().ToString(), "merge_1");
      db_iter->Prev();
      ASSERT_TRUE(!db_iter->Valid());
    }

    {
      TestIterator* internal_iter = new TestIterator(BytewiseComparator());
      internal_iter->AddMerge("a", "merge_1");
      internal_iter->AddPut("b", "val");
      internal_iter->AddMerge("b", "merge_2");

      internal_iter->AddDeletion("b");
      internal_iter->AddMerge("b", "merge_3");

      internal_iter->AddMerge("c", "merge_4");
      internal_iter->AddMerge("c", "merge_5");

      internal_iter->AddDeletion("b");
      internal_iter->AddMerge("b", "merge_6");
      internal_iter->AddMerge("b", "merge_7");
      internal_iter->AddMerge("b", "merge_8");
      internal_iter->AddMerge("b", "merge_9");
      internal_iter->AddMerge("b", "merge_10");
      internal_iter->AddMerge("b", "merge_11");

      internal_iter->AddDeletion("c");
      internal_iter->Finish();

      std::unique_ptr<Iterator> db_iter(NewDBIterator(
          env_, options, BytewiseComparator(), internal_iter, 13));
      db_iter->SeekToLast();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_EQ(db_iter->key().ToString(), "c");
      ASSERT_EQ(db_iter->value().ToString(), "merge_4,merge_5");
      db_iter->Prev();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_TRUE(db_iter->Valid());
      ASSERT_EQ(db_iter->key().ToString(), "b");
      ASSERT_EQ(db_iter->value().ToString(),
                "merge_6,merge_7,merge_8,merge_9,merge_10,merge_11");
      db_iter->Prev();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_EQ(db_iter->key().ToString(), "a");
      ASSERT_EQ(db_iter->value().ToString(), "merge_1");
      db_iter->Prev();
      ASSERT_TRUE(!db_iter->Valid());
    }

    {
      TestIterator* internal_iter = new TestIterator(BytewiseComparator());
      internal_iter->AddMerge("a", "merge_1");
      internal_iter->AddPut("b", "val");
      internal_iter->AddMerge("b", "merge_2");

      internal_iter->AddDeletion("b");
      internal_iter->AddMerge("b", "merge_3");

      internal_iter->AddMerge("c", "merge_4");
      internal_iter->AddMerge("c", "merge_5");

      internal_iter->AddDeletion("b");
      internal_iter->AddMerge("b", "merge_6");
      internal_iter->AddMerge("b", "merge_7");
      internal_iter->AddMerge("b", "merge_8");
      internal_iter->AddMerge("b", "merge_9");
      internal_iter->AddMerge("b", "merge_10");
      internal_iter->AddMerge("b", "merge_11");

      internal_iter->AddDeletion("c");
      internal_iter->Finish();

      std::unique_ptr<Iterator> db_iter(NewDBIterator(
          env_, options, BytewiseComparator(), internal_iter, 14));
      db_iter->SeekToLast();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_EQ(db_iter->key().ToString(), "b");
      ASSERT_EQ(db_iter->value().ToString(),
                "merge_6,merge_7,merge_8,merge_9,merge_10,merge_11");
      db_iter->Prev();
      ASSERT_TRUE(db_iter->Valid());

      ASSERT_EQ(db_iter->key().ToString(), "a");
      ASSERT_EQ(db_iter->value().ToString(), "merge_1");
      db_iter->Prev();
      ASSERT_TRUE(!db_iter->Valid());
    }
  }

  {
    Options options;
    TestIterator* internal_iter = new TestIterator(BytewiseComparator());
    internal_iter->AddDeletion("a");
    internal_iter->AddPut("a", "0");
    internal_iter->AddPut("b", "0");
    internal_iter->Finish();

    std::unique_ptr<Iterator> db_iter(
        NewDBIterator(env_, options, BytewiseComparator(), internal_iter, 10));
    db_iter->SeekToLast();
    ASSERT_TRUE(db_iter->Valid());
    ASSERT_EQ(db_iter->key().ToString(), "b");
    ASSERT_EQ(db_iter->value().ToString(), "0");

    db_iter->Prev();
    ASSERT_TRUE(db_iter->Valid());
    ASSERT_EQ(db_iter->key().ToString(), "a");
    ASSERT_EQ(db_iter->value().ToString(), "0");
  }
}

}  // namespace rocksdb

int main(int argc, char** argv) { return rocksdb::test::RunAllTests(); }
