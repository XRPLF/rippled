//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/basics/StringUtilities.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/random.h>
#include <ripple/beast/unit_test.h>
#include <ripple/beast/xor_shift_engine.h>
#include <ripple/protocol/digest.h>
#include <ripple/shamap/SHAMap.h>
#include <ripple/shamap/SHAMapSyncFilter.h>
#include <functional>
#include <stdexcept>
#include <test/shamap/common.h>
#include <test/unit_test/SuiteJournal.h>

namespace ripple {
namespace tests {

class FetchPack_test : public beast::unit_test::suite
{
public:
    enum { tableItems = 100, tableItemsExtra = 20 };

    using Map = hash_map<SHAMapHash, Blob>;
    using Table = SHAMap;
    using Item = SHAMapItem;

    struct Handler
    {
        void
        operator()(std::uint32_t refNum) const
        {
            Throw<std::runtime_error>("missing node");
        }
    };

    struct TestFilter : SHAMapSyncFilter
    {
        TestFilter(Map& map, beast::Journal journal)
            : mMap(map), mJournal(journal)
        {
        }

        void
        gotNode(
            bool fromFilter,
            SHAMapHash const& nodeHash,
            std::uint32_t ledgerSeq,
            Blob&& nodeData,
            SHAMapNodeType type) const override
        {
        }

        boost::optional<Blob>
        getNode(SHAMapHash const& nodeHash) const override
        {
            Map::iterator it = mMap.find(nodeHash);
            if (it == mMap.end())
            {
                JLOG(mJournal.fatal()) << "Test filter missing node";
                return boost::none;
            }
            return it->second;
        }

        Map& mMap;
        beast::Journal mJournal;
    };

    std::shared_ptr<Item>
    make_random_item(beast::xor_shift_engine& r)
    {
        Serializer s;
        for (int d = 0; d < 3; ++d)
            s.add32(ripple::rand_int<std::uint32_t>(r));
        return std::make_shared<Item>(s.getSHA512Half(), s.peekData());
    }

    void
    add_random_items(std::size_t n, Table& t, beast::xor_shift_engine& r)
    {
        while (n--)
        {
            std::shared_ptr<SHAMapItem> item(make_random_item(r));
            auto const result(
                t.addItem(SHAMapNodeType::tnACCOUNT_STATE, std::move(*item)));
            assert(result);
            (void)result;
        }
    }

    void
    on_fetch(Map& map, SHAMapHash const& hash, Blob const& blob)
    {
        BEAST_EXPECT(sha512Half(makeSlice(blob)) == hash.as_uint256());
        map.emplace(hash, blob);
    }

    void
    run() override
    {
        using namespace beast::severities;
        test::SuiteJournal journal("FetchPack_test", *this);

        TestNodeFamily f(journal);
        std::shared_ptr<Table> t1(std::make_shared<Table>(SHAMapType::FREE, f));

        pass();

        //         beast::Random r;
        //         add_random_items (tableItems, *t1, r);
        //         std::shared_ptr <Table> t2 (t1->snapShot (true));
        //
        //         add_random_items (tableItemsExtra, *t1, r);
        //         add_random_items (tableItemsExtra, *t2, r);

        // turn t1 into t2
        //         Map map;
        //         t2->getFetchPack (t1.get(), true, 1000000, std::bind (
        //             &FetchPack_test::on_fetch, this, std::ref (map),
        //             std::placeholders::_1, std::placeholders::_2));
        //         t1->getFetchPack (nullptr, true, 1000000, std::bind (
        //             &FetchPack_test::on_fetch, this, std::ref (map),
        //             std::placeholders::_1, std::placeholders::_2));

        // try to rebuild t2 from the fetch pack
        //         std::shared_ptr <Table> t3;
        //         try
        //         {
        //             TestFilter filter (map, beast::Journal());
        //
        //             t3 = std::make_shared <Table> (SHAMapType::FREE,
        //             t2->getHash (),
        //                 fullBelowCache);
        //
        //             BEAST_EXPECT(t3->fetchRoot (t2->getHash (), &filter),
        //             "unable to get root");
        //
        //             // everything should be in the pack, no hashes should be
        //             needed std::vector <uint256> hashes =
        //             t3->getNeededHashes(1, &filter);
        //             BEAST_EXPECT(hashes.empty(), "missing hashes");
        //
        //             BEAST_EXPECT(t3->getHash () == t2->getHash (), "root
        //             hashes do not match"); BEAST_EXPECT(t3->deepCompare
        //             (*t2), "failed compare");
        //         }
        //         catch (std::exception const&)
        //         {
        //             fail ("unhandled exception");
        //         }
    }
};

BEAST_DEFINE_TESTSUITE(FetchPack, shamap, ripple);

}  // namespace tests
}  // namespace ripple
