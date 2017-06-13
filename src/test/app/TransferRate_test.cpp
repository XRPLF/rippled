//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2017 Ripple Labs Inc.

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
#include <test/jtx.h>
#include <ripple/beast/unit_test.h>
#include <algorithm>

namespace ripple {
namespace test {

class TransferRate_test : public beast::unit_test::suite
{
public:
    void
    test (std::uint32_t tr, std::uint32_t aliceBal)
    {
        using namespace jtx;
        Env env(*this);
        auto const gw = Account("gateway");
        auto const USD = gw["USD"];
        env.fund(XRP(10000), gw, "alice", "bob");
        env.trust(USD(2), "alice", "bob");
        env.close();
		auto jt = noop("alice");
        jt[sfTransferRate.fieldName] = tr;
        env(jt);
        env.close();
        env(pay(gw, "alice", USD(2)));
        env(pay("alice", "bob", USD(1)));
        env.require(balance("alice", USD(aliceBal)));
        env.require(balance("bob", USD(1)));
    }

    void
    run() override
    {
        test(1000000000, 1);
        test(2000000000, 0);
    }
};

BEAST_DEFINE_TESTSUITE(TransferRate,tx,ripple);

} // test
} // ripple

