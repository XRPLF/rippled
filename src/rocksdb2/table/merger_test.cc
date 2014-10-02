//  Copyright (c) 2013, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.

#include <vector>
#include <string>
#include <algorithm>

#include "rocksdb/iterator.h"
#include "table/merger.h"
#include "util/testharness.h"
#include "util/testutil.h"

namespace rocksdb {

class VectorIterator : public Iterator {
 public:
  explicit VectorIterator(const std::vector<std::string>& keys)
      : keys_(keys), current_(keys.size()) {
    std::sort(keys_.begin(), keys_.end());
  }

  virtual bool Valid() const { return current_ < keys_.size(); }

  virtual void SeekToFirst() { current_ = 0; }
  virtual void SeekToLast() { current_ = keys_.size() - 1; }

  virtual void Seek(const Slice& target) {
    current_ = std::lower_bound(keys_.begin(), keys_.end(), target.ToString()) -
               keys_.begin();
  }

  virtual void Next() { current_++; }
  virtual void Prev() { current_--; }

  virtual Slice key() const { return Slice(keys_[current_]); }
  virtual Slice value() const { return Slice(); }

  virtual Status status() const { return Status::OK(); }

 private:
  std::vector<std::string> keys_;
  size_t current_;
};

class MergerTest {
 public:
  MergerTest()
      : rnd_(3), merging_iterator_(nullptr), single_iterator_(nullptr) {}
  ~MergerTest() = default;
  std::vector<std::string> GenerateStrings(int len, int string_len) {
    std::vector<std::string> ret;
    for (int i = 0; i < len; ++i) {
      ret.push_back(test::RandomHumanReadableString(&rnd_, string_len));
    }
    return ret;
  }

  void AssertEquivalence() {
    auto a = merging_iterator_.get();
    auto b = single_iterator_.get();
    if (!a->Valid()) {
      ASSERT_TRUE(!b->Valid());
    } else {
      ASSERT_TRUE(b->Valid());
      ASSERT_EQ(b->key().ToString(), a->key().ToString());
      ASSERT_EQ(b->value().ToString(), a->value().ToString());
    }
  }

  void SeekToRandom() { Seek(test::RandomHumanReadableString(&rnd_, 5)); }

  void Seek(std::string target) {
    merging_iterator_->Seek(target);
    single_iterator_->Seek(target);
  }

  void SeekToFirst() {
    merging_iterator_->SeekToFirst();
    single_iterator_->SeekToFirst();
  }

  void SeekToLast() {
    merging_iterator_->SeekToLast();
    single_iterator_->SeekToLast();
  }

  void Next(int times) {
    for (int i = 0; i < times && merging_iterator_->Valid(); ++i) {
      AssertEquivalence();
      merging_iterator_->Next();
      single_iterator_->Next();
    }
    AssertEquivalence();
  }

  void Prev(int times) {
    for (int i = 0; i < times && merging_iterator_->Valid(); ++i) {
      AssertEquivalence();
      merging_iterator_->Prev();
      single_iterator_->Prev();
    }
    AssertEquivalence();
  }

  void NextAndPrev(int times) {
    for (int i = 0; i < times && merging_iterator_->Valid(); ++i) {
      AssertEquivalence();
      if (rnd_.OneIn(2)) {
        merging_iterator_->Prev();
        single_iterator_->Prev();
      } else {
        merging_iterator_->Next();
        single_iterator_->Next();
      }
    }
    AssertEquivalence();
  }

  void Generate(size_t num_iterators, size_t strings_per_iterator,
                size_t letters_per_string) {
    std::vector<Iterator*> small_iterators;
    for (size_t i = 0; i < num_iterators; ++i) {
      auto strings = GenerateStrings(strings_per_iterator, letters_per_string);
      small_iterators.push_back(new VectorIterator(strings));
      all_keys_.insert(all_keys_.end(), strings.begin(), strings.end());
    }

    merging_iterator_.reset(NewMergingIterator(
        BytewiseComparator(), &small_iterators[0], small_iterators.size()));
    single_iterator_.reset(new VectorIterator(all_keys_));
  }

  Random rnd_;
  std::unique_ptr<Iterator> merging_iterator_;
  std::unique_ptr<Iterator> single_iterator_;
  std::vector<std::string> all_keys_;
};

TEST(MergerTest, SeekToRandomNextTest) {
  Generate(1000, 50, 50);
  for (int i = 0; i < 10; ++i) {
    SeekToRandom();
    AssertEquivalence();
    Next(50000);
  }
}

TEST(MergerTest, SeekToRandomNextSmallStringsTest) {
  Generate(1000, 50, 2);
  for (int i = 0; i < 10; ++i) {
    SeekToRandom();
    AssertEquivalence();
    Next(50000);
  }
}

TEST(MergerTest, SeekToRandomPrevTest) {
  Generate(1000, 50, 50);
  for (int i = 0; i < 10; ++i) {
    SeekToRandom();
    AssertEquivalence();
    Prev(50000);
  }
}

TEST(MergerTest, SeekToRandomRandomTest) {
  Generate(200, 50, 50);
  for (int i = 0; i < 3; ++i) {
    SeekToRandom();
    AssertEquivalence();
    NextAndPrev(5000);
  }
}

TEST(MergerTest, SeekToFirstTest) {
  Generate(1000, 50, 50);
  for (int i = 0; i < 10; ++i) {
    SeekToFirst();
    AssertEquivalence();
    Next(50000);
  }
}

TEST(MergerTest, SeekToLastTest) {
  Generate(1000, 50, 50);
  for (int i = 0; i < 10; ++i) {
    SeekToLast();
    AssertEquivalence();
    Prev(50000);
  }
}

}  // namespace rocksdb

int main(int argc, char** argv) { return rocksdb::test::RunAllTests(); }
