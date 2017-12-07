//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include "db/db_test_util.h"
#include "db/write_batch_internal.h"
#include "db/write_thread.h"
#include "port/port.h"
#include "port/stack_trace.h"
#include "util/fault_injection_test_env.h"
#include "util/string_util.h"
#include "util/sync_point.h"

namespace rocksdb {

// Test variations of WriteImpl.
class DBWriteTest : public DBTestBase, public testing::WithParamInterface<int> {
 public:
  DBWriteTest() : DBTestBase("/db_write_test") {}

  Options GetOptions() { return DBTestBase::GetOptions(GetParam()); }

  void Open() { DBTestBase::Reopen(GetOptions()); }
};

// Sequence number should be return through input write batch.
TEST_P(DBWriteTest, ReturnSeuqneceNumber) {
  Random rnd(4422);
  Open();
  for (int i = 0; i < 100; i++) {
    WriteBatch batch;
    batch.Put("key" + ToString(i), test::RandomHumanReadableString(&rnd, 10));
    ASSERT_OK(dbfull()->Write(WriteOptions(), &batch));
    ASSERT_EQ(dbfull()->GetLatestSequenceNumber(),
              WriteBatchInternal::Sequence(&batch));
  }
}

TEST_P(DBWriteTest, ReturnSeuqneceNumberMultiThreaded) {
  constexpr size_t kThreads = 16;
  constexpr size_t kNumKeys = 1000;
  Open();
  ASSERT_EQ(0, dbfull()->GetLatestSequenceNumber());
  // Check each sequence is used once and only once.
  std::vector<std::atomic_flag> flags(kNumKeys * kThreads + 1);
  for (size_t i = 0; i < flags.size(); i++) {
    flags[i].clear();
  }
  auto writer = [&](size_t id) {
    Random rnd(4422 + static_cast<uint32_t>(id));
    for (size_t k = 0; k < kNumKeys; k++) {
      WriteBatch batch;
      batch.Put("key" + ToString(id) + "-" + ToString(k),
                test::RandomHumanReadableString(&rnd, 10));
      ASSERT_OK(dbfull()->Write(WriteOptions(), &batch));
      SequenceNumber sequence = WriteBatchInternal::Sequence(&batch);
      ASSERT_GT(sequence, 0);
      ASSERT_LE(sequence, kNumKeys * kThreads);
      // The sequence isn't consumed by someone else.
      ASSERT_FALSE(flags[sequence].test_and_set());
    }
  };
  std::vector<port::Thread> threads;
  for (size_t i = 0; i < kThreads; i++) {
    threads.emplace_back(writer, i);
  }
  for (size_t i = 0; i < kThreads; i++) {
    threads[i].join();
  }
}

TEST_P(DBWriteTest, IOErrorOnWALWritePropagateToWriteThreadFollower) {
  constexpr int kNumThreads = 5;
  std::unique_ptr<FaultInjectionTestEnv> mock_env(
      new FaultInjectionTestEnv(Env::Default()));
  Options options = GetOptions();
  options.env = mock_env.get();
  Reopen(options);
  std::atomic<int> ready_count{0};
  std::atomic<int> leader_count{0};
  std::vector<port::Thread> threads;
  mock_env->SetFilesystemActive(false);
  // Wait until all threads linked to write threads, to make sure
  // all threads join the same batch group.
  SyncPoint::GetInstance()->SetCallBack(
      "WriteThread::JoinBatchGroup:Wait", [&](void* arg) {
        ready_count++;
        auto* w = reinterpret_cast<WriteThread::Writer*>(arg);
        if (w->state == WriteThread::STATE_GROUP_LEADER) {
          leader_count++;
          while (ready_count < kNumThreads) {
            // busy waiting
          }
        }
      });
  SyncPoint::GetInstance()->EnableProcessing();
  for (int i = 0; i < kNumThreads; i++) {
    threads.push_back(port::Thread(
        [&](int index) {
          // All threads should fail.
          ASSERT_FALSE(Put("key" + ToString(index), "value").ok());
        },
        i));
  }
  for (int i = 0; i < kNumThreads; i++) {
    threads[i].join();
  }
  ASSERT_EQ(1, leader_count);
  // Close before mock_env destruct.
  Close();
}

INSTANTIATE_TEST_CASE_P(DBWriteTestInstance, DBWriteTest,
                        testing::Values(DBTestBase::kDefault,
                                        DBTestBase::kConcurrentWALWrites,
                                        DBTestBase::kPipelinedWrite));

}  // namespace rocksdb

int main(int argc, char** argv) {
  rocksdb::port::InstallStackTraceHandler();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
