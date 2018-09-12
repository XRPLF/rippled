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

#include <ripple/peerfinder/impl/Livecache.h>
#include <ripple/basics/chrono.h>
#include <ripple/beast/unit_test.h>
#include <ripple/beast/clock/manual_clock.h>
#include <test/beast/IPEndpointCommon.h>
#include <test/unit_test/SuiteJournal.h>
#include <boost/algorithm/string.hpp>

namespace ripple {
namespace PeerFinder {

bool operator== (Endpoint const& a, Endpoint const& b)
{
    return (a.hops == b.hops &&
            a.address == b.address);
}

class Livecache_test : public beast::unit_test::suite
{
    TestStopwatch clock_;
    test::SuiteJournal journal_;

public:
    Livecache_test()
    : journal_ ("Livecache_test", *this)
    { }

    // Add the address as an endpoint
    template <class C>
    inline void add (beast::IP::Endpoint ep, C& c, int hops = 0)
    {
        Endpoint cep {ep, hops};
        c.insert (cep);
    }

    void testBasicInsert ()
    {
        testcase ("Basic Insert");
        Livecache <> c (clock_, journal_);
        BEAST_EXPECT(c.empty());

        for (auto i = 0; i < 10; ++i)
            add(beast::IP::randomEP(true), c);

        BEAST_EXPECT(! c.empty());
        BEAST_EXPECT(c.size() == 10);

        for (auto i = 0; i < 10; ++i)
            add(beast::IP::randomEP(false), c);

        BEAST_EXPECT(! c.empty());
        BEAST_EXPECT(c.size() == 20);
    }

    void testInsertUpdate ()
    {
        testcase ("Insert/Update");
        Livecache <> c (clock_, journal_);

        auto ep1 = Endpoint {beast::IP::randomEP(), 2};
        c.insert(ep1);
        BEAST_EXPECT(c.size() == 1);
        // third position list will contain the entry
        BEAST_EXPECT((c.hops.begin()+2)->begin()->hops == 2);

        auto ep2 = Endpoint {ep1.address, 4};
        // this will not change the entry has higher hops
        c.insert(ep2);
        BEAST_EXPECT(c.size() == 1);
        // still in third position list
        BEAST_EXPECT((c.hops.begin()+2)->begin()->hops == 2);

        auto ep3 = Endpoint {ep1.address, 2};
        // this will not change the entry has the same hops as existing
        c.insert(ep3);
        BEAST_EXPECT(c.size() == 1);
        // still in third position list
        BEAST_EXPECT((c.hops.begin()+2)->begin()->hops == 2);

        auto ep4 = Endpoint {ep1.address, 1};
        c.insert(ep4);
        BEAST_EXPECT(c.size() == 1);
        // now at second position list
        BEAST_EXPECT((c.hops.begin()+1)->begin()->hops == 1);
    }

    void testExpire ()
    {
        testcase ("Expire");
        using namespace std::chrono_literals;
        Livecache <> c (clock_, journal_);

        auto ep1 = Endpoint {beast::IP::randomEP(), 1};
        c.insert(ep1);
        BEAST_EXPECT(c.size() == 1);
        c.expire();
        BEAST_EXPECT(c.size() == 1);
        // verify that advancing to 1 sec before expiration
        // leaves our entry intact
        clock_.advance(Tuning::liveCacheSecondsToLive - 1s);
        c.expire();
        BEAST_EXPECT(c.size() == 1);
        // now advance to the point of expiration
        clock_.advance(1s);
        c.expire();
        BEAST_EXPECT(c.empty());
    }

    void testHistogram ()
    {
        testcase ("Histogram");
        constexpr auto num_eps = 40;
        Livecache <> c (clock_, journal_);
        for (auto i = 0; i < num_eps; ++i)
            add(
                beast::IP::randomEP(true),
                c,
                ripple::rand_int(0, static_cast<int>(Tuning::maxHops + 1)));
        auto h = c.hops.histogram();
        if(! BEAST_EXPECT(! h.empty()))
            return;
        std::vector <std::string> v;
        boost::split (v, h, boost::algorithm::is_any_of (","));
        auto sum = 0;
        for (auto const& n : v)
        {
            auto val = boost::lexical_cast<int>(boost::trim_copy(n));
            sum += val;
            BEAST_EXPECT(val >= 0);
        }
        BEAST_EXPECT(sum == num_eps);
    }


    void testShuffle ()
    {
        testcase ("Shuffle");
        Livecache <> c (clock_, journal_);
        for (auto i = 0; i < 100; ++i)
            add(
                beast::IP::randomEP(true),
                c,
                ripple::rand_int(0, static_cast<int>(Tuning::maxHops + 1)));

        using at_hop = std::vector <ripple::PeerFinder::Endpoint>;
        using all_hops = std::array <at_hop, 1 + Tuning::maxHops + 1>;

        auto cmp_EP = [](Endpoint const& a, Endpoint const& b) {
            return (b.hops < a.hops || (b.hops == a.hops && b.address < a.address));
        };
        all_hops before;
        all_hops before_sorted;
        for (auto i = std::make_pair(0, c.hops.begin());
                i.second != c.hops.end(); ++i.first, ++i.second)
        {
            std::copy ((*i.second).begin(), (*i.second).end(),
                std::back_inserter (before[i.first]));
            std::copy ((*i.second).begin(), (*i.second).end(),
                std::back_inserter (before_sorted[i.first]));
            std::sort(
                before_sorted[i.first].begin(),
                before_sorted[i.first].end(),
                cmp_EP);
        }

        c.hops.shuffle();

        all_hops after;
        all_hops after_sorted;
        for (auto i = std::make_pair(0, c.hops.begin());
                i.second != c.hops.end(); ++i.first, ++i.second)
        {
            std::copy ((*i.second).begin(), (*i.second).end(),
                std::back_inserter (after[i.first]));
            std::copy ((*i.second).begin(), (*i.second).end(),
                std::back_inserter (after_sorted[i.first]));
            std::sort(
                after_sorted[i.first].begin(),
                after_sorted[i.first].end(),
                cmp_EP);
        }

        // each hop bucket should contain the same items
        // before and after sort, albeit in different order
        bool all_match = true;
        for (auto i = 0; i < before.size(); ++i)
        {
            BEAST_EXPECT(before[i].size() == after[i].size());
            all_match = all_match && (before[i] == after[i]);
            BEAST_EXPECT(before_sorted[i] == after_sorted[i]);
        }
        BEAST_EXPECT(! all_match);
    }

    void run () override
    {
        testBasicInsert ();
        testInsertUpdate ();
        testExpire ();
        testHistogram ();
        testShuffle ();
    }
};

BEAST_DEFINE_TESTSUITE(Livecache,peerfinder,ripple);

}
}
