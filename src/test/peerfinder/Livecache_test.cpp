#include <test/beast/IPEndpointCommon.h>
#include <test/unit_test/SuiteJournal.h>

#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/peerfinder/detail/Livecache.h>
#include <xrpld/peerfinder/detail/Tuning.h>

#include <xrpl/basics/chrono.h>
#include <xrpl/basics/random.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/unit_test/suite.h>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/lexical_cast.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <utility>
#include <vector>

namespace xrpl::PeerFinder {

bool
operator==(Endpoint const& a, Endpoint const& b)
{
    return (a.hops == b.hops && a.address == b.address);
}

class Livecache_test : public beast::unit_test::Suite
{
    TestStopwatch clock_;
    test::SuiteJournal journal_;

public:
    Livecache_test() : journal_("Livecache_test", *this)
    {
    }

    // Add the address as an endpoint
    template <class C>
    void
    add(beast::IP::Endpoint ep, C& c, std::uint32_t hops = 0)
    {
        Endpoint const cep{ep, hops};
        c.insert(cep);
    }

    void
    testBasicInsert()
    {
        testcase("Basic Insert");
        Livecache<> c(clock_, journal_);
        BEAST_EXPECT(c.empty());

        for (auto i = 0; i < 10; ++i)
            add(beast::IP::randomEP(true), c);

        BEAST_EXPECT(!c.empty());
        BEAST_EXPECT(c.size() == 10);

        for (auto i = 0; i < 10; ++i)
            add(beast::IP::randomEP(false), c);

        BEAST_EXPECT(!c.empty());
        BEAST_EXPECT(c.size() == 20);
    }

    void
    testInsertUpdate()
    {
        testcase("Insert/Update");
        Livecache<> c(clock_, journal_);

        auto ep1 = Endpoint{beast::IP::randomEP(), 2};
        c.insert(ep1);
        BEAST_EXPECT(c.size() == 1);
        // third position list will contain the entry
        BEAST_EXPECT((c.hops.begin() + 2)->begin()->hops == 2);

        auto ep2 = Endpoint{ep1.address, 4};
        // this will not change the entry has higher hops
        c.insert(ep2);
        BEAST_EXPECT(c.size() == 1);
        // still in third position list
        BEAST_EXPECT((c.hops.begin() + 2)->begin()->hops == 2);

        auto ep3 = Endpoint{ep1.address, 2};
        // this will not change the entry has the same hops as existing
        c.insert(ep3);
        BEAST_EXPECT(c.size() == 1);
        // still in third position list
        BEAST_EXPECT((c.hops.begin() + 2)->begin()->hops == 2);

        auto ep4 = Endpoint{ep1.address, 1};
        c.insert(ep4);
        BEAST_EXPECT(c.size() == 1);
        // now at second position list
        BEAST_EXPECT((c.hops.begin() + 1)->begin()->hops == 1);
    }

    void
    testExpire()
    {
        testcase("Expire");
        using namespace std::chrono_literals;
        Livecache<> c(clock_, journal_);

        auto ep1 = Endpoint{beast::IP::randomEP(), 1};
        c.insert(ep1);
        BEAST_EXPECT(c.size() == 1);
        c.expire();
        BEAST_EXPECT(c.size() == 1);
        // verify that advancing to 1 sec before expiration
        // leaves our entry intact
        clock_.advance(Tuning::kLiveCacheSecondsToLive - 1s);
        c.expire();
        BEAST_EXPECT(c.size() == 1);
        // now advance to the point of expiration
        clock_.advance(1s);
        c.expire();
        BEAST_EXPECT(c.empty());
    }

    void
    testHistogram()
    {
        testcase("Histogram");
        static constexpr auto kNumEps = 40;
        Livecache<> c(clock_, journal_);
        for (auto i = 0; i < kNumEps; ++i)
            add(beast::IP::randomEP(true), c, xrpl::randInt<std::uint32_t>());
        auto h = c.hops.histogram();
        if (!BEAST_EXPECT(!h.empty()))
            return;
        std::vector<std::string> v;
        boost::split(v, h, boost::algorithm::is_any_of(","));
        auto sum = 0;
        for (auto const& n : v)
        {
            auto val = boost::lexical_cast<int>(boost::trim_copy(n));
            sum += val;
            BEAST_EXPECT(val >= 0);
        }
        BEAST_EXPECT(sum == kNumEps);
    }

    void
    testShuffle()
    {
        testcase("Shuffle");
        Livecache<> c(clock_, journal_);
        for (auto i = 0; i < 100; ++i)
            add(beast::IP::randomEP(true), c, xrpl::randInt(Tuning::kMaxHops + 1));

        using at_hop = std::vector<xrpl::PeerFinder::Endpoint>;
        using all_hops = std::array<at_hop, 1 + Tuning::kMaxHops + 1>;

        auto cmpEp = [](Endpoint const& a, Endpoint const& b) {
            return (b.hops < a.hops || (b.hops == a.hops && b.address < a.address));
        };
        all_hops before;
        all_hops beforeSorted;
        for (auto i = std::make_pair(0, c.hops.begin()); i.second != c.hops.end();
             ++i.first, ++i.second)
        {
            std::copy((*i.second).begin(), (*i.second).end(), std::back_inserter(before[i.first]));
            std::copy(
                (*i.second).begin(), (*i.second).end(), std::back_inserter(beforeSorted[i.first]));
            std::sort(beforeSorted[i.first].begin(), beforeSorted[i.first].end(), cmpEp);
        }

        c.hops.shuffle();

        all_hops after;
        all_hops afterSorted;
        for (auto i = std::make_pair(0, c.hops.begin()); i.second != c.hops.end();
             ++i.first, ++i.second)
        {
            std::copy((*i.second).begin(), (*i.second).end(), std::back_inserter(after[i.first]));
            std::copy(
                (*i.second).begin(), (*i.second).end(), std::back_inserter(afterSorted[i.first]));
            std::sort(afterSorted[i.first].begin(), afterSorted[i.first].end(), cmpEp);
        }

        // each hop bucket should contain the same items
        // before and after sort, albeit in different order
        bool allMatch = true;
        for (auto i = 0; i < before.size(); ++i)
        {
            BEAST_EXPECT(before[i].size() == after[i].size());
            allMatch = allMatch && (before[i] == after[i]);
            BEAST_EXPECT(beforeSorted[i] == afterSorted[i]);
        }
        BEAST_EXPECT(!allMatch);
    }

    void
    run() override
    {
        testBasicInsert();
        testInsertUpdate();
        testExpire();
        testHistogram();
        testShuffle();
    }
};

BEAST_DEFINE_TESTSUITE(Livecache, peerfinder, xrpl);

}  // namespace xrpl::PeerFinder
