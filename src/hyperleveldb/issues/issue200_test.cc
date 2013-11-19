// Test for issue 200: iterator reversal brings in keys newer than snapshots
#include <iostream>
#include <sstream>
#include <cstdlib>

#include "hyperleveldb/db.h"
#include "hyperleveldb/write_batch.h"
#include "util/testharness.h"

namespace {

class Issue200 { };

TEST(Issue200, Test) {
  // Get rid of any state from an old run.
  std::string dbpath = leveldb::test::TmpDir() + "/leveldb_200_iterator_test";
  DestroyDB(dbpath, leveldb::Options());

  // Open database.
  leveldb::DB* db;
  leveldb::Options db_options;
  db_options.create_if_missing = true;
  db_options.compression = leveldb::kNoCompression;
  ASSERT_OK(leveldb::DB::Open(db_options, dbpath, &db));

  leveldb::WriteOptions write_options;
  db->Put(write_options, leveldb::Slice("1", 1), leveldb::Slice("b", 1));
  db->Put(write_options, leveldb::Slice("2", 1), leveldb::Slice("c", 1));
  db->Put(write_options, leveldb::Slice("3", 1), leveldb::Slice("d", 1));
  db->Put(write_options, leveldb::Slice("4", 1), leveldb::Slice("e", 1));
  db->Put(write_options, leveldb::Slice("5", 1), leveldb::Slice("f", 1));

  const leveldb::Snapshot *snapshot = db->GetSnapshot();

  leveldb::ReadOptions read_options;
  read_options.snapshot = snapshot;
  leveldb::Iterator *iter = db->NewIterator(read_options);

  // Commenting out this Put changes the iterator behavior,
  // gives the expected behavior. This is unexpected because
  // the iterator is taken on a snapshot that was taken
  // before the Put
  db->Put(write_options, leveldb::Slice("25", 2), leveldb::Slice("cd", 2));

  iter->Seek(leveldb::Slice("5", 1));
  ASSERT_EQ("5", iter->key().ToString());
  iter->Prev();
  ASSERT_EQ("4", iter->key().ToString());
  iter->Prev();
  ASSERT_EQ("3", iter->key().ToString());

  // At this point the iterator is at "3", I expect the Next() call will
  // move it to "4". But it stays on "3"
  iter->Next();
  ASSERT_EQ("4", iter->key().ToString());
  iter->Next();
  ASSERT_EQ("5", iter->key().ToString());

  // close database
  delete iter;
  db->ReleaseSnapshot(snapshot);
  delete db;
  DestroyDB(dbpath, leveldb::Options());
}

}  // anonymous namespace

int main(int argc, char** argv) {
  return leveldb::test::RunAllTests();
}
