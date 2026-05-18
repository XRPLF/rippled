#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/random.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/xor_shift_engine.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapMissingNode.h>
#include <xrpl/shamap/SHAMapSyncFilter.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <boost/smart_ptr/intrusive_ptr.hpp>

#include <gtest/gtest.h>
#include <helpers/TestSink.h>
#include <shamap/common.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>

namespace xrpl::tests {

class FetchPackTest : public ::testing::Test
{
protected:
    beast::Journal const j_{TestSink::instance()};

    static constexpr auto kTableItems = 100;
    static constexpr auto kTableItemsExtra = 20;

    using Map = hash_map<SHAMapHash, Blob>;
    using Table = SHAMap;
    using Item = SHAMapItem;

    struct Handler
    {
        void
        operator()(std::uint32_t refNum) const
        {
            (void)refNum;
            Throw<std::runtime_error>("missing node");
        }
    };

    struct TestFilter : SHAMapSyncFilter
    {
        TestFilter(Map& map, beast::Journal journal) : map(map), journal(journal)
        {
        }

        void
        gotNode(
            [[maybe_unused]] bool fromFilter,
            [[maybe_unused]] SHAMapHash const& nodeHash,
            [[maybe_unused]] std::uint32_t ledgerSeq,
            // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
            [[maybe_unused]] Blob&& nodeData,
            [[maybe_unused]] SHAMapNodeType type) const override
        {
        }

        [[nodiscard]] std::optional<Blob>
        getNode(SHAMapHash const& nodeHash) const override
        {
            Map::iterator const it = map.find(nodeHash);
            if (it == map.end())
            {
                JLOG(journal.fatal()) << "Test filter missing node";
                return std::nullopt;
            }
            return it->second;
        }

        Map& map;
        beast::Journal journal;
    };

    static boost::intrusive_ptr<Item>
    makeRandomItemMember(beast::xor_shift_engine& r)
    {
        Serializer s;
        for (int d = 0; d < 3; ++d)
            s.add32(xrpl::randInt<std::uint32_t>(r));
        return makeShamapitem(s.getSHA512Half(), s.slice());
    }

    static void
    addRandomItems(std::size_t n, Table& t, beast::xor_shift_engine& r)
    {
        while ((n--) != 0u)
        {
            auto const result(t.addItem(SHAMapNodeType::TnAccountState, makeRandomItemMember(r)));
            assert(result);
            (void)result;
        }
    }

    static void
    onFetch(Map& map, SHAMapHash const& hash, Blob const& blob)
    {
        EXPECT_EQ(sha512Half(makeSlice(blob)), hash.asUInt256());
        map.emplace(hash, blob);
    }
};

TEST_F(FetchPackTest, construct_table)
{
    TestNodeFamily f(j_);
    std::shared_ptr<Table> const t1(std::make_shared<Table>(SHAMapType::FREE, f));

    EXPECT_NE(t1, nullptr);
}

}  // namespace xrpl::tests
