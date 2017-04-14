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
#include <test/jtx.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/JsonFields.h>

namespace ripple {
namespace test {

struct SetAuth_test : public beast::unit_test::suite
{
    // Set just the tfSetfAuth flag on a trust line
    // If the trust line does not exist, then it should
    // be created under the new rules.
    static
    Json::Value
    auth (jtx::Account const& account,
        jtx::Account const& dest,
            std::string const& currency)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::Account] = account.human();
        jv[jss::LimitAmount] = STAmount(
            { to_currency(currency), dest }).getJson(0);
        jv[jss::TransactionType] = "TrustSet";
        jv[jss::Flags] = tfSetfAuth;
        return jv;
    }

    void testAuth(std::initializer_list<uint256> fs)
    {
        using namespace jtx;
        auto const gw = Account("gw");
        auto const USD = gw["USD"];
        {
            Env env(*this, features(fs));
            env.fund(XRP(100000), "alice", gw);
            env(fset(gw, asfRequireAuth));
            env(auth(gw, "alice", "USD"),       ter(tecNO_LINE_REDUNDANT));
        }
        {
            Env env(*this, features(featureTrustSetAuth));
            env.fund(XRP(100000), "alice", "bob", gw);
            env(fset(gw, asfRequireAuth));
            env(auth(gw, "alice", "USD"));
            BEAST_EXPECT(env.le(
                keylet::line(Account("alice").id(),
                    gw.id(), USD.currency)));
            env(trust("alice", USD(1000)));
            env(trust("bob", USD(1000)));
            env(pay(gw, "alice", USD(100)));
            env(pay(gw, "bob", USD(100)),       ter(tecPATH_DRY)); // Should be terNO_AUTH
            env(pay("alice", "bob", USD(50)),   ter(tecPATH_DRY)); // Should be terNO_AUTH
        }
    }

    void run() override
    {
        testAuth({});
        testAuth({featureFlow});
        testAuth({featureFlow, fix1373});
    }
};

BEAST_DEFINE_TESTSUITE(SetAuth,test,ripple);

} // test
} // ripple
