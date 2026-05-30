// Plan 7 Phase-2 — differential test for SHAMap::updateHashesParallel.
//
// The contract: updateHashesParallel(W) must return the *byte-identical* root
// hash that the serial getHash() produces, for any workload and any worker
// count. We assert this against the production serial path on independently
// mutated twin maps, over replace / insert / erase / mixed workloads, many
// randomized seeds, and W in {1,2,4,8,16}.

#include <helpers/TestFamily.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapMissingNode.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

namespace xrpl::test {

namespace {

constexpr std::array<int, 5> kWorkerCounts{1, 2, 4, 8, 16};

[[nodiscard]] uint256
randomKey(std::mt19937_64& rng)
{
    uint256 k;
    auto* p = k.data();
    for (std::size_t i = 0; i < k.size(); i += 8)
    {
        std::uint64_t const r = rng();
        std::memcpy(p + i, &r, 8);
    }
    return k;
}

[[nodiscard]] boost::intrusive_ptr<SHAMapItem const>
makeItem(uint256 const& key, std::uint64_t salt)
{
    std::array<std::uint8_t, 96> buf{};
    std::memcpy(buf.data(), &salt, sizeof(salt));
    std::memcpy(buf.data() + sizeof(salt), key.data(), 16);
    return makeShamapitem(key, Slice(buf.data(), buf.size()));
}

}  // namespace

class UpdateHashesParallel : public ::testing::Test
{
protected:
    TestFamily family_{beast::Journal{beast::Journal::getNullSink()}};

    // A settled (hashes computed, nodes shared) base map of N random entries.
    // Snapshots of it are the mutation targets — each snapshot clones on first
    // touch, exactly like a live ledger inheriting its parent's state.
    std::shared_ptr<SHAMap>
    makeBase(std::size_t N, std::uint64_t seed, std::vector<uint256>& keysOut)
    {
        std::mt19937_64 rng(seed);
        auto base = std::make_shared<SHAMap>(SHAMapType::STATE, family_);
        base->setUnbacked();
        keysOut.clear();
        keysOut.reserve(N);
        for (std::size_t i = 0; i < N; ++i)
        {
            uint256 const k = randomKey(rng);
            keysOut.push_back(k);
            base->addItem(SHAMapNodeType::TnAccountState, makeItem(k, 0));
        }
        base->getHash();  // settle
        return base;
    }

    // Assert: for every worker count, a freshly mutated snapshot hashed in
    // parallel equals an identically mutated snapshot hashed serially.
    template <class Mutate>
    void
    expectParallelMatchesSerial(
        std::shared_ptr<SHAMap> const& base,
        Mutate&& mutate,
        char const* label)
    {
        auto serialMap = base->snapShot(/*isMutable=*/true);
        mutate(*serialMap);
        SHAMapHash const serial = serialMap->getHash();

        for (int w : kWorkerCounts)
        {
            auto parMap = base->snapShot(/*isMutable=*/true);
            mutate(*parMap);
            SHAMapHash const par = parMap->updateHashesParallel(w);
            EXPECT_EQ(serial, par)
                << label << " mismatch at workers=" << w;
            // A second hash must agree with the cached result it left behind.
            EXPECT_EQ(serial, parMap->getHash())
                << label << " post-parallel getHash mismatch at workers=" << w;
        }
    }
};

TEST_F(UpdateHashesParallel, ReplaceWorkload)
{
    std::vector<uint256> keys;
    auto base = makeBase(/*N=*/5000, /*seed=*/0x11, keys);
    std::mt19937_64 rng(0xA1);
    std::uniform_int_distribution<std::size_t> pick(0, keys.size() - 1);

    for (std::size_t M : {1u, 50u, 500u, 2000u})
    {
        std::vector<uint256> sel;
        for (std::size_t i = 0; i < M; ++i)
            sel.push_back(keys[pick(rng)]);

        expectParallelMatchesSerial(
            base,
            [&](SHAMap& m) {
                std::uint64_t salt = 1;
                for (auto const& k : sel)
                    m.updateGiveItem(
                        SHAMapNodeType::TnAccountState, makeItem(k, salt++));
            },
            "replace");
    }
}

TEST_F(UpdateHashesParallel, InsertWorkload)
{
    std::vector<uint256> keys;
    auto base = makeBase(/*N=*/3000, /*seed=*/0x22, keys);

    for (std::size_t M : {1u, 100u, 1500u})
    {
        // Distinct fresh keys generated from a fixed seed so both twin
        // snapshots receive the identical insert set.
        std::mt19937_64 keyRng(0xBEEF + M);
        std::vector<uint256> fresh;
        for (std::size_t i = 0; i < M; ++i)
            fresh.push_back(randomKey(keyRng));

        expectParallelMatchesSerial(
            base,
            [&](SHAMap& m) {
                std::uint64_t salt = 1;
                for (auto const& k : fresh)
                    m.addItem(
                        SHAMapNodeType::TnAccountState, makeItem(k, salt++));
            },
            "insert");
    }
}

TEST_F(UpdateHashesParallel, EraseWorkload)
{
    std::vector<uint256> keys;
    auto base = makeBase(/*N=*/4000, /*seed=*/0x33, keys);

    for (std::size_t M : {1u, 100u, 1000u})
    {
        // Erase the first M keys (a deterministic, distinct subset).
        std::vector<uint256> sel(keys.begin(), keys.begin() + M);
        expectParallelMatchesSerial(
            base,
            [&](SHAMap& m) {
                for (auto const& k : sel)
                    m.delItem(k);
            },
            "erase");
    }
}

TEST_F(UpdateHashesParallel, MixedWorkload)
{
    std::vector<uint256> keys;
    auto base = makeBase(/*N=*/6000, /*seed=*/0x44, keys);
    std::mt19937_64 keyRng(0xC0DE);
    std::vector<uint256> fresh;
    for (int i = 0; i < 800; ++i)
        fresh.push_back(randomKey(keyRng));

    expectParallelMatchesSerial(
        base,
        [&](SHAMap& m) {
            std::uint64_t salt = 1;
            for (std::size_t i = 0; i < 800; ++i)
            {
                m.updateGiveItem(
                    SHAMapNodeType::TnAccountState, makeItem(keys[i], salt++));
                m.delItem(keys[3000 + i]);
                m.addItem(
                    SHAMapNodeType::TnAccountState, makeItem(fresh[i], salt++));
            }
        },
        "mixed");
}

TEST_F(UpdateHashesParallel, NoMutationsAndEmpty)
{
    // Clean snapshot: no dirty nodes, must return the inherited root hash.
    std::vector<uint256> keys;
    auto base = makeBase(/*N=*/2000, /*seed=*/0x55, keys);
    auto clean = base->snapShot(/*isMutable=*/true);
    for (int w : kWorkerCounts)
        EXPECT_EQ(base->getHash(), clean->updateHashesParallel(w));

    // Empty map.
    auto empty = std::make_shared<SHAMap>(SHAMapType::STATE, family_);
    empty->setUnbacked();
    for (int w : kWorkerCounts)
        EXPECT_EQ(empty->getHash(), empty->updateHashesParallel(w));
}

}  // namespace xrpl::test
