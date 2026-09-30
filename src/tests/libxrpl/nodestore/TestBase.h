#pragma once

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/random.h>
#include <xrpl/beast/utility/rngfill.h>
#include <xrpl/beast/xor_shift_engine.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Types.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace xrpl::node_store {

constexpr std::size_t kMinPayloadBytes = 1;
constexpr std::size_t kMaxPayloadBytes = 2000;
constexpr int kNumObjectsToTest = 2000;
constexpr int kNumObjects = 2000;
constexpr std::uint64_t kSeedValue = 50;

struct LessThan
{
    bool
    operator()(std::shared_ptr<NodeObject> const& lhs, std::shared_ptr<NodeObject> const& rhs)
        const noexcept
    {
        return lhs->getHash() < rhs->getHash();
    }
};

[[nodiscard]] inline bool
isSame(std::shared_ptr<NodeObject> const& lhs, std::shared_ptr<NodeObject> const& rhs)
{
    return (lhs->getType() == rhs->getType()) && (lhs->getHash() == rhs->getHash()) &&
        (lhs->getData() == rhs->getData());
}

[[nodiscard]] inline Batch
createPredictableBatch(std::size_t numObjects, std::uint64_t seed)
{
    Batch batch;
    batch.reserve(numObjects);

    beast::xor_shift_engine rng(seed);

    for (auto i = 0uz; i < numObjects; ++i)
    {
        NodeObjectType const type = [&] {
            switch (randInt(rng, 3))
            {
                case 0:
                    return NodeObjectType::Ledger;
                case 1:
                    return NodeObjectType::AccountNode;
                case 2:
                    return NodeObjectType::TransactionNode;
                case 3:
                default:
                    return NodeObjectType::Unknown;
            }
        }();

        uint256 hash;
        beast::rngfill(hash.begin(), hash.size(), rng);

        Blob blob(randInt(rng, kMinPayloadBytes, kMaxPayloadBytes));
        beast::rngfill(blob.data(), blob.size(), rng);

        batch.emplace_back(NodeObject::createObject(type, std::move(blob), hash));
    }

    return batch;
}

inline void
storeBatch(Backend& backend, Batch const& batch)
{
    for (auto const& obj : batch)
        backend.store(obj);
}

[[nodiscard]] inline Batch
fetchCopyOfBatch(Backend& backend, Batch const& batch)
{
    Batch copy;
    copy.reserve(batch.size());

    for (auto i = 0uz; i < batch.size(); ++i)
    {
        SCOPED_TRACE("fetchCopyOfBatch index=" + std::to_string(i));
        std::shared_ptr<NodeObject> object;
        Status const status = backend.fetch(batch[i]->getHash(), &object);
        EXPECT_EQ(status, Status::Ok);
        if (status == Status::Ok)
        {
            EXPECT_NE(object, nullptr);
            copy.emplace_back(object);
        }
    }
    return copy;
}

inline void
fetchMissing(Backend& backend, Batch const& batch)
{
    for (auto i = 0uz; i < batch.size(); ++i)
    {
        SCOPED_TRACE("fetchMissing index=" + std::to_string(i));
        std::shared_ptr<NodeObject> object;
        Status const status = backend.fetch(batch[i]->getHash(), &object);
        EXPECT_EQ(status, Status::NotFound);
    }
}

inline void
storeBatch(Database& db, Batch const& batch)
{
    for (auto const& obj : batch)
    {
        Blob data(obj->getData());
        db.store(obj->getType(), std::move(data), obj->getHash(), db.earliestLedgerSeq());
    }
}

[[nodiscard]] inline Batch
fetchCopyOfBatch(Database& db, Batch const& batch)
{
    Batch copy;
    copy.reserve(batch.size());

    for (auto const& obj : batch)
    {
        std::shared_ptr<NodeObject> const result = db.fetchNodeObject(obj->getHash(), 0);
        if (result != nullptr)
            copy.emplace_back(result);
    }
    return copy;
}

inline void
fetchMissing(Database& db, Batch const& batch)
{
    for (auto i = 0uz; i < batch.size(); ++i)
    {
        SCOPED_TRACE("fetchMissing(Database) index=" + std::to_string(i));
        EXPECT_EQ(db.fetchNodeObject(batch[i]->getHash(), 0), nullptr);
    }
}

}  // namespace xrpl::node_store

namespace xrpl {

[[nodiscard]] inline bool
operator==(node_store::Batch const& lhs, node_store::Batch const& rhs)
{
    return std::ranges::equal(lhs, rhs, node_store::isSame);
}

}  // namespace xrpl
