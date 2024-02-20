//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

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

#include <ripple/protocol/Feature.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>

namespace ripple {
namespace test {

class Batch_test : public beast::unit_test::suite
{
    void
    testBatch(FeatureBitset features)
    {
        testcase("batch");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};
        auto const feeDrops = env.current()->fees().base;
        // Env env{
        //     *this,
        //     envconfig(),
        //     features,
        //     nullptr,
        //     // beast::severities::kWarning
        //     beast::severities::kTrace};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

        auto const seq = env.seq("alice");

        // ttBATCH
        Json::Value jv;
        jv[jss::TransactionType] = jss::Batch;
        jv[jss::Account] = alice.human();

        // Batch Transactions
        jv[sfTransactions.jsonName] = Json::Value{Json::arrayValue};
        
        // Tx 1
        jv[sfTransactions.jsonName][0U] = Json::Value{};
        jv[sfTransactions.jsonName][0U][jss::BatchTransaction] = Json::Value{};
        jv[sfTransactions.jsonName][0U][jss::BatchTransaction]
          [jss::TransactionType] = jss::AccountSet;
        jv[sfTransactions.jsonName][0U][jss::BatchTransaction]
          [sfAccount.jsonName] = alice.human();
        jv[sfTransactions.jsonName][0U][jss::BatchTransaction][sfFee.jsonName] =
            to_string(feeDrops);
        jv[sfTransactions.jsonName][0U][jss::BatchTransaction][jss::Sequence] =
            seq + 1;
        jv[sfTransactions.jsonName][0U][jss::BatchTransaction]
          [jss::SigningPubKey] = strHex(alice.pk());
        
        // Tx 2
        jv[sfTransactions.jsonName][1U] = Json::Value{};
        jv[sfTransactions.jsonName][1U][jss::BatchTransaction] = Json::Value{};
        jv[sfTransactions.jsonName][1U][jss::BatchTransaction]
          [jss::TransactionType] = jss::AccountSet;
        jv[sfTransactions.jsonName][1U][jss::BatchTransaction]
          [sfAccount.jsonName] = alice.human();
        jv[sfTransactions.jsonName][1U][jss::BatchTransaction][sfFee.jsonName] =
            to_string(feeDrops);
        jv[sfTransactions.jsonName][1U][jss::BatchTransaction][jss::Sequence] =
            seq + 2;
        jv[sfTransactions.jsonName][1U][jss::BatchTransaction]
          [jss::SigningPubKey] = strHex(alice.pk());

        env(jv, fee(drops((2 * feeDrops) + (2 * feeDrops))), ter(tesSUCCESS));
        env.close();
    }

    void
    testInvalidBatch(FeatureBitset features)
    {
        testcase("invalid batch");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

        Json::Value jv;
        jv[jss::TransactionType] = jss::AccountSet;
        jv[jss::Account] = alice.human();
        jv[jss::Destination] = bob.human();
        jv[sfFee.jsonName] = "10";
        jv[sfCloseResolution.jsonName] = to_string(1);
        env(jv, fee(drops(10)), ter(tesSUCCESS));
        env.close();
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testBatch(features);
        // testInvalidBatch(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        auto const sa = supported_amendments();
        testWithFeats(sa);
    }
};

BEAST_DEFINE_TESTSUITE(Batch, app, ripple);

}  // namespace test
}  // namespace ripple
