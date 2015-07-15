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

#include <BeastConfig.h>
#include <ripple/basics/chrono.h>
#include <ripple/peerfinder/impl/Logic.h>
#include <beast/unit_test/suite.h>

namespace ripple {
namespace PeerFinder {

class Logic_test : public beast::unit_test::suite
{
public:
    struct TestStore : Store
    {
        std::size_t
        load (load_callback const& cb) override
        {
            return 0;
        }

        void
        save (std::vector <Entry> const&) override
        {
        }
    };

    struct TestChecker
    {
        void
        stop()
        {
        }

        void
        wait()
        {
        }

        template <class Handler>
        void
        async_connect (beast::IP::Endpoint const& ep,
            Handler&& handler)
        {
            boost::system::error_code ec;
            handler (ep, ep, ec);
        }
    };

    void
    test_backoff1()
    {
        auto const seconds = 10000;
        testcase("backoff 1");
        TestStore store;
        TestChecker checker;
        TestStopwatch clock;
        Logic<TestChecker> logic (clock, store, checker, beast::Journal{});
        logic.addFixedPeer ("test",
            beast::IP::Endpoint::from_string("65.0.0.1:5"));
        {
            Config c;
            c.autoConnect = false;
            c.listeningPort = 1024;
            logic.config(c);
        }
        std::size_t n = 0;
        for (std::size_t i = 0; i < seconds; ++i)
        {
            auto const list = logic.autoconnect();
            if (! list.empty())
            {
                expect (list.size() == 1);
                auto const slot = logic.new_outbound_slot(list.front());
                expect (logic.onConnected(slot,
                    beast::IP::Endpoint::from_string("65.0.0.2:5")));
                logic.on_closed(slot);
                ++n;
            }
            clock.advance(std::chrono::seconds(1));
            logic.once_per_second();
        }
        // Less than 20 attempts
        expect (n < 20, "backoff");
    }

    // with activate
    void
    test_backoff2()
    {
        auto const seconds = 10000;
        testcase("backoff 2");
        TestStore store;
        TestChecker checker;
        TestStopwatch clock;
        Logic<TestChecker> logic (clock, store, checker, beast::Journal{});
        logic.addFixedPeer ("test",
            beast::IP::Endpoint::from_string("65.0.0.1:5"));
        {
            Config c;
            c.autoConnect = false;
            c.listeningPort = 1024;
            logic.config(c);
        }
        std::size_t n = 0;
        std::array<std::uint8_t, 33> key;
        key.fill(0);
        for (std::size_t i = 0; i < seconds; ++i)
        {
            auto const list = logic.autoconnect();
            if (! list.empty())
            {
                expect (list.size() == 1);
                auto const slot = logic.new_outbound_slot(list.front());
                if (! expect (logic.onConnected(slot,
                        beast::IP::Endpoint::from_string("65.0.0.2:5"))))
                    return;
                std::string s = ".";
                RipplePublicKey pk (key.begin(), key.end());
                if (! expect (logic.activate(slot, pk, false) ==
                        PeerFinder::Result::success, "activate"))
                    return;
                logic.on_closed(slot);
                ++n;
            }
            clock.advance(std::chrono::seconds(1));
            logic.once_per_second();
        }
        // No more often than once per minute
        expect (n <= (seconds+59)/60, "backoff");
    }

    void run ()
    {
        test_backoff1();
        test_backoff2();
    }
};

BEAST_DEFINE_TESTSUITE(Logic,PeerFinder,ripple);

}
}
