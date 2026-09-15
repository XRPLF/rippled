#include <xrpl/basics/Blob.h>
#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/DummyScheduler.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/Types.h>
#include <xrpl/nodestore/detail/DatabaseRotatingImp.h>

#include <gtest/gtest.h>
#include <helpers/TestSink.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace xrpl::node_store {
namespace {

std::shared_ptr<NodeObject>
makeObject(std::uint8_t seed)
{
    Blob data(32, seed);
    uint256 hash;
    std::ranges::fill(hash, seed);
    return NodeObject::createObject(NodeObjectType::AccountNode, std::move(data), hash);
}

/**
 * A rotating database over two in-memory backends the test can reach
 * directly, so an object can be planted in the archive alone.
 */
class RotatingDatabaseTest : public ::testing::Test
{
protected:
    void
    SetUp() override
    {
        // The memory factory keeps one store per path for the whole process,
        // so each backend, and each test, gets its own path.
        std::string const prefix = std::string("rotating-") +
            ::testing::UnitTest::GetInstance()->current_test_info()->name();
        Section writableParams;
        writableParams.set("type", "memory");
        writableParams.set("path", prefix + "-writable");
        Section archiveParams;
        archiveParams.set("type", "memory");
        archiveParams.set("path", prefix + "-archive");

        writable_ =
            Manager::instance().makeBackend(writableParams, megabytes(4), scheduler_, journal_);
        archive_ =
            Manager::instance().makeBackend(archiveParams, megabytes(4), scheduler_, journal_);
        ASSERT_NE(writable_, nullptr);
        ASSERT_NE(archive_, nullptr);
        writable_->open();
        archive_->open();
        db_ = std::make_unique<DatabaseRotatingImp>(
            scheduler_, 2, writable_, archive_, writableParams, journal_);
    }

    bool
    inWritable(uint256 const& hash)
    {
        std::shared_ptr<NodeObject> out;
        return writable_->fetch(hash, &out) == Status::Ok && out != nullptr;
    }

    DummyScheduler scheduler_;
    beast::Journal const journal_{TestSink::instance()};
    std::shared_ptr<Backend> writable_;
    std::shared_ptr<Backend> archive_;
    std::unique_ptr<DatabaseRotatingImp> db_;
};

TEST_F(RotatingDatabaseTest, duplicate_fetch_from_archive_counts_one_copy_forward_and_writes_it)
{
    auto const object = makeObject(1);
    archive_->store(object);
    ASSERT_FALSE(inWritable(object->getHash()));
    ASSERT_EQ(db_->duplicateCopyForwardTotal(), 0u);

    auto const fetched =
        db_->fetchNodeObject(object->getHash(), 0, FetchType::Synchronous, /*duplicate=*/true);
    ASSERT_NE(fetched, nullptr);
    EXPECT_EQ(fetched->getHash(), object->getHash());

    // Served by the archive on a duplicate fetch: copied once, and only on
    // this total. The ordinary-read total is for reads made without
    // duplicate while a rotation is in flight, and none happened.
    EXPECT_EQ(db_->duplicateCopyForwardTotal(), 1u);
    EXPECT_EQ(db_->copyForwardTotal(), 0u);
    EXPECT_TRUE(inWritable(object->getHash()));

    // Now it is in the writable backend, so a second duplicate fetch copies
    // nothing.
    ASSERT_NE(
        db_->fetchNodeObject(object->getHash(), 0, FetchType::Synchronous, /*duplicate=*/true),
        nullptr);
    EXPECT_EQ(db_->duplicateCopyForwardTotal(), 1u);
}

TEST_F(RotatingDatabaseTest, duplicate_fetch_served_by_writable_counts_nothing)
{
    auto const object = makeObject(2);
    writable_->store(object);

    ASSERT_NE(
        db_->fetchNodeObject(object->getHash(), 0, FetchType::Synchronous, /*duplicate=*/true),
        nullptr);
    EXPECT_EQ(db_->duplicateCopyForwardTotal(), 0u);
    EXPECT_EQ(db_->copyForwardTotal(), 0u);
}

TEST_F(RotatingDatabaseTest, ordinary_fetch_during_rotation_does_not_count_as_duplicate_copy)
{
    auto const object = makeObject(3);
    archive_->store(object);
    db_->setRotationInFlight(true);

    ASSERT_NE(
        db_->fetchNodeObject(object->getHash(), 0, FetchType::Synchronous, /*duplicate=*/false),
        nullptr);

    // Copied forward because a rotation is in flight, but that is the other
    // total's event. Which total moved is exactly what tells the rotation's
    // own fetches apart from reads the rest of the node happened to make.
    EXPECT_EQ(db_->duplicateCopyForwardTotal(), 0u);
    EXPECT_TRUE(inWritable(object->getHash()));
#ifdef XRPL_ENABLE_TELEMETRY
    EXPECT_EQ(db_->copyForwardTotal(), 1u);
#endif
    db_->setRotationInFlight(false);
}

}  // namespace
}  // namespace xrpl::node_store
