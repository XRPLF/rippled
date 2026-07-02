#include <test/shamap/common.h>
#include <test/unit_test/SuiteJournal.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/random.h>
#include <xrpl/beast/unit_test/suite.h>
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

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace xrpl::tests {

class FetchPack_test : public beast::unit_test::Suite
{
public:
    static constexpr auto kTableItems = 100;
    static constexpr auto kTableItemsExtra = 20;

    using Map = hash_map<SHAMapHash, Blob>;
    using Table = SHAMap;
    using Item = SHAMapItem;

    struct TestFilter : SHAMapSyncFilter
    {
        TestFilter(Map& map, beast::Journal journal) : map(map), journal(journal)
        {
        }

        void
        gotNode(
            bool fromFilter,
            SHAMapHash const& nodeHash,
            std::uint32_t ledgerSeq,
            Blob&& nodeData,  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
            SHAMapNodeType type) const override
        {
        }

        [[nodiscard]] std::optional<Blob>
        getNode(SHAMapHash const& nodeHash) const override
        {
            auto const it = map.find(nodeHash);
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

    void
    onFetch(Map& map, SHAMapHash const& hash, Blob const& blob)
    {
        BEAST_EXPECT(sha512Half(makeSlice(blob)) == hash.asUInt256());
        map.emplace(hash, blob);
    }

    void
    run() override
    {
        testFetchPack();
    }

    // Exercises a fetch-pack round trip: build a SHAMap, serialize every node
    // into a pack keyed by node hash, then rebuild the map in a fresh SHAMap by
    // sourcing every node from the pack through a SHAMapSyncFilter and comparing
    // the result. This covers the filter-based reconstruction path (fetchRoot +
    // getMissingNodes with a SHAMapSyncFilter), complementing SHAMapSync_test,
    // which drives the getNodeFat/addKnownNode path.
    void
    testFetchPack()
    {
        test::SuiteJournal journal("FetchPack_test", *this);
        TestNodeFamily f(journal), f2(journal);
        beast::xor_shift_engine r;

        // Build a source map. getHash() unshares the tree and computes every
        // node hash; this must happen before serializing nodes below, otherwise
        // inner nodes still carry stale cached hashes.
        auto const source = std::make_shared<Table>(SHAMapType::FREE, f);
        addRandomItems(kTableItems + kTableItemsExtra, *source, r);
        source->setImmutable();
        auto const rootHash = source->getHash();

        // Turn the source into a fetch pack: node hash -> serialized node.
        Map map;
        source->visitNodes([this, &map](SHAMapTreeNode& node) {
            Serializer s;
            node.serializeWithPrefix(s);
            onFetch(map, node.getHash(), s.getData());
            return true;
        });

        // Rebuild the map in a fresh family, sourcing every node from the pack
        // through the SHAMapSyncFilter.
        auto const rebuilt = std::make_shared<Table>(SHAMapType::FREE, rootHash.asUInt256(), f2);
        TestFilter filter(map, journal);
        rebuilt->setSynching();
        BEAST_EXPECT(rebuilt->fetchRoot(rootHash, &filter));

        // Everything should be in the pack, so no nodes should be missing.
        BEAST_EXPECT(rebuilt->getMissingNodes(2048, &filter).empty());
        rebuilt->clearSynching();

        BEAST_EXPECT(rebuilt->deepCompare(*source));
    }
};

BEAST_DEFINE_TESTSUITE(FetchPack, shamap, xrpl);

}  // namespace xrpl::tests
