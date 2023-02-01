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

#include <ripple/basics/chrono.h>
#include <ripple/ledger/Directory.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
// #include <ripple/protocol/URIToken.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>

#include <chrono>

namespace ripple {
namespace test {
struct URIToken_test : public beast::unit_test::suite
{
    static uint256
    tokenid(
        jtx::Account const& account,
        std::string const& uri)
    {
        auto const k = keylet::uritoken(account, Blob(uri.begin(), uri.end()));
        return k.key;
    }

    static bool
    inOwnerDir(ReadView const& view, jtx::Account const& acct, std::shared_ptr<SLE const> const& token)
    {
        ripple::Dir const ownerDir(view, keylet::ownerDir(acct.id()));
        return std::find(ownerDir.begin(), ownerDir.end(), token) != ownerDir.end();
    }

    static std::size_t
    ownerDirCount(ReadView const& view, jtx::Account const& acct)
    {
        ripple::Dir const ownerDir(view, keylet::ownerDir(acct.id()));
        return std::distance(ownerDir.begin(), ownerDir.end());
    };

    static Json::Value
    mint(
        jtx::Account const& account,
        std::string const& uri)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::TransactionType] = jss::URIToken;
        jv[jss::Account] = account.human();
        jv[sfURI.jsonName] = strHex(uri);
        return jv;
    }

    static Json::Value
    burn(
        jtx::Account const& account,
        std::string const& id)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::TransactionType] = jss::URIToken;
        jv[jss::Account] = account.human();
        jv[sfURITokenID.jsonName] = id;
        return jv;
    }

    static Json::Value
    buy(
        jtx::Account const& account,
        std::string const& id,
        STAmount const& amount)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::TransactionType] = jss::URIToken;
        jv[jss::Account] = account.human();
        jv[jss::Amount] = amount.getJson(JsonOptions::none);
        jv[sfURITokenID.jsonName] = id;
        return jv;
    }

    static Json::Value
    sell(
        jtx::Account const& account,
        std::string const& id,
        STAmount const& amount)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::TransactionType] = jss::URIToken;
        jv[jss::Account] = account.human();
        jv[jss::Amount] = amount.getJson(JsonOptions::none);
        jv[sfURITokenID.jsonName] = id;
        return jv;
    }

    static Json::Value
    clear(
        jtx::Account const& account,
        std::string const& id)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::TransactionType] = jss::URIToken;
        jv[jss::Account] = account.human();
        jv[sfURITokenID.jsonName] = id;
        return jv;
    }

    void
    testEnabled(FeatureBitset features)
    {
        testcase("enabled");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // setup env
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        
        {
            // If the URIToken amendment is not enabled, you should not be able
            // to mint, burn, buy, sell or clear uri tokens.
            Env env{*this, features - featureURIToken};

            env.fund(XRP(1000), alice, bob);

            std::string const uri(maxTokenURILength, '?');
            std::string const id{strHex(tokenid(alice, uri))};

            // MINT
            env(mint(alice, uri), ter(temDISABLED));
            env.close();
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 0);
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 0);

            // SELL
            env(sell(alice, id, XRP(10)), txflags(tfSell), ter(temDISABLED));
            env.close();
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 0);
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 0);

            // BUY
            env(buy(bob, id, XRP(10)), ter(temDISABLED));
            env.close();
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 0);
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 0);

            // SELL
            env(sell(bob, id, XRP(10)), txflags(tfSell), ter(temDISABLED));
            env.close();
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 0);
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 0);

            // CLEAR
            env(clear(bob, id), ter(temDISABLED));
            env.close();
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 0);
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 0);

            // BURN
            env(burn(bob, id), ter(temDISABLED));
            env.close();
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 0);
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 0);
        }
        {
            // If the URIToken amendment is enabled, you should be able
            // to mint, burn, buy, sell and clear uri tokens.
            Env env{*this, features};

            env.fund(XRP(1000), alice, bob);

            std::string const uri(maxTokenURILength, '?');
            std::string const id{strHex(tokenid(alice, uri))};

            // MINT
            env(mint(alice, uri));
            env.close();
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 1);
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 0);

            // SELL
            env(sell(alice, id, XRP(10)), txflags(tfSell));
            env.close();
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 1);
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 0);

            // BUY
            env(buy(bob, id, XRP(10)));
            env.close();
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 0);
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 1);

            // SELL
            env(sell(bob, id, XRP(10)), txflags(tfSell));
            env.close();
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 0);
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 1);

            // CLEAR
            env(clear(bob, id));
            env.close();
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 0);
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 1);

            // BURN
            env(burn(bob, id));
            env.close();
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 0);
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 0);
        }
    }

    void
    testMintInvalid(FeatureBitset features)
    {
        testcase("mint_invalid");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // setup env
        Env env{*this, features};
        auto const alice = Account("alice");
        env.fund(XRP(200), alice);
        env.close();

        //----------------------------------------------------------------------
        // preflight
        // temDISABLED - ignored
        std::string const uri(2, '?');

        // tecINSUFFICIENT_RESERVE - out of xrp
        env(mint(alice, uri), ter(tecINSUFFICIENT_RESERVE));
        env.close();

        // pay alice xrp
        env(pay(env.master, alice, XRP(1000)));
        env.close();

        // temMALFORMED - no uri & no flags
        std::string const nouri(0, '?');
        env(mint(alice, nouri), ter(temMALFORMED));
        env.close();

        // temMALFORMED - bad uri 257 len
        std::string const longuri(maxTokenURILength + 1, '?');
        env(mint(alice, longuri), ter(temMALFORMED));
        env.close();

        //----------------------------------------------------------------------
        // preclaim

        // tecNO_ENTRY - no id & not mint operation
        env(mint(alice, uri), txflags(tfSell), ter(temMALFORMED));
        env.close();
        // tecDUPLICATE - duplicate uri token
        env(mint(alice, uri));
        env(mint(alice, uri), ter(tecDUPLICATE));
        env.close();

        //----------------------------------------------------------------------
        // doApply

        // tecDUPLICATE - duplicate uri token
        env(mint(alice, uri), ter(tecDUPLICATE));
        env.close();
        // tecDIR_FULL - directory full
    }

    void
    testBurnInvalid(FeatureBitset features)
    {
        testcase("burn_invalid");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // setup env
        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const dave = Account("dave");
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

        //----------------------------------------------------------------------
        // preflight
        // temDISABLED - ignore

        // mint non burnable token
        std::string const uri(2, '?');
        std::string const id{strHex(tokenid(alice, uri))};
        env(mint(alice, uri));
        env.close();

        // temMALFORMED - bad flags
        env(burn(alice, id), txflags(0b001100010U), ter(temMALFORMED));
        env.close();

        // tecNO_PERMISSION - not owner and not (issuer/burnable)
        env(burn(bob, id), txflags(tfBurn), ter(tecNO_PERMISSION));
        env.close();

        //----------------------------------------------------------------------
        // entry preclaim

        // tecNO_ENTRY - no item
        std::string const baduri(3, '?');
        std::string const badid{strHex(tokenid(alice, baduri))};
        env(burn(alice, badid), txflags(tfBurn), ter(tecNO_ENTRY));
        env.close();
        // tecNO_ENTRY - no owner
        env(burn(dave, id), txflags(tfBurn), ter(tecNO_ENTRY));
        env.close();

        //----------------------------------------------------------------------
        // doApply

        // tecNO_PERMISSION - no permission
        env(burn(carol, id), txflags(tfBurn), ter(tecNO_PERMISSION));
        env.close();
        // tefBAD_LEDGER - could not remove object
    }

    void
    testSellInvalid(FeatureBitset features)
    {
        testcase("sell_invalid");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // setup env
        Env env{*this, features};
        auto const nacct = Account("alice");
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];
        auto const NUSD = nacct["USD"];
        env.fund(XRP(1000), alice, bob, gw);
        env.trust(USD(10000), alice);
        env.trust(USD(10000), bob);
        env.close();
        env(pay(gw, alice, USD(1000)));
        env(pay(gw, bob, USD(1000)));
        env.close();

        // mint token
        std::string const uri(2, '?');
        std::string const id{strHex(tokenid(alice, uri))};
        env(mint(alice, uri));
        env.close();

        //----------------------------------------------------------------------
        // operator preflight
        // temDISABLED

        // temMALFORMED - invalid sell flag
        env(sell(alice, id, USD(10)), txflags(0b000110101U), ter(temMALFORMED));
        env.close();

        //----------------------------------------------------------------------
        // amount preflight
        // temBAD_AMOUNT - bad xrp/amount
        env(sell(alice, id, XRP(-1)), txflags(tfSell), ter(temBAD_AMOUNT));
        // temBAD_AMOUNT - bad ft/amount
        env(sell(alice, id, USD(-1)), txflags(tfSell), ter(temBAD_AMOUNT));
        // temBAD_CURRENCY - bad currency
        IOU const BAD{gw, badCurrency()};
        env(sell(alice, id, BAD(10)), txflags(tfSell), ter(temBAD_CURRENCY));
        env.close();

        //----------------------------------------------------------------------
        // preclaim
        // tecNO_PERMISSION - invalid account
        env(sell(bob, id, USD(10)), txflags(tfSell), ter(tecNO_PERMISSION));
        // tecNO_ISSUER - invalid issuer
        // env(sell(alice, id, NUSD(10)), txflags(tfSell), ter(tecNO_ISSUER));

        //----------------------------------------------------------------------
        // doApply

        // tecNO_PERMISSION - invalid account
        env(sell(bob, id, USD(10)), txflags(tfSell), ter(tecNO_PERMISSION));
    }

    void
    testBuyInvalid(FeatureBitset features)
    {
        testcase("buy_invalid");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // setup env
        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];
        env.fund(XRP(1000), alice, bob, carol, gw);
        env.trust(USD(10000), alice);
        env.trust(USD(10000), bob);
        env.trust(USD(10000), carol);
        env.close();
        env(pay(gw, alice, USD(1000)));
        env(pay(gw, bob, USD(1000)));
        env(pay(gw, carol, USD(1000)));
        env.close();

        // mint token
        std::string const uri(2, '?');
        std::string const id{strHex(tokenid(alice, uri))};
        env(mint(alice, uri));
        env.close();

        //----------------------------------------------------------------------
        // operator preflight
        // temDISABLED

        // temMALFORMED - invalid buy flag
        env(buy(bob, id, USD(10)), txflags(0b000110011U), ter(temMALFORMED));
        env.close();

        //----------------------------------------------------------------------
        // preclaim
        // tecNO_PERMISSION - not for sale
        env(buy(bob, id, USD(10)), ter(tecNO_PERMISSION));
        env.close();

        // set sell
        env(sell(alice, id, USD(10)), txflags(tfSell), jtx::token::destination(bob));
        env.close();

        // tecNO_PERMISSION - for sale to dest, you are not dest
        env(buy(carol, id, USD(10)), ter(tecNO_PERMISSION));
        env.close();
        
        // tecNFTOKEN_BUY_SELL_MISMATCH - invalid buy sell amounts
        env(buy(bob, id, EUR(10)), ter(tecNFTOKEN_BUY_SELL_MISMATCH));
        env.close();

        // tecINSUFFICIENT_PAYMENT - insuficient buy offer amount
        env(buy(bob, id, USD(9)), ter(tecINSUFFICIENT_PAYMENT));
        env.close();

        env(clear(alice, id));
        env(sell(alice, id, XRP(10000)), txflags(tfSell));
        env.close();

        // tecINSUFFICIENT_FUNDS - insuficient xrp - fees
        env(buy(bob, id, XRP(1000)), ter(tecINSUFFICIENT_PAYMENT));
        env.close();

        // clear sell and reset new sell
        env(clear(alice, id));
        env(sell(alice, id, USD(10000)), txflags(tfSell));
        env.close();

        // tecINSUFFICIENT_FUNDS - insuficient amount
        env(buy(bob, id, USD(1000)), ter(tecINSUFFICIENT_PAYMENT));
        env.close();

        //----------------------------------------------------------------------
        // doApply

        // tecNO_PERMISSION - not listed
        // tecNO_PERMISSION - for sale to dest, you are not dest
        // tecNFTOKEN_BUY_SELL_MISMATCH - invalid buy sell amounts
        // tecINSUFFICIENT_PAYMENT - insuficient xrp
        // tecINSUFFICIENT_FUNDS - insuficient xrp - fees
        // tecINSUFFICIENT_FUNDS - insuficient amount
        // tecNO_LINE_INSUF_RESERVE - insuficient amount
        // tecDIR_FULL - unknown how to test/handle
        // tecINTERNAL - unknown how to test/handle
        // tecINTERNAL - unknown how to test/handle
        // tecINTERNAL - unknown how to test/handle
        // tecINTERNAL - unknown how to test/handle
        // tefBAD_LEDGER - unknown how to test/handle
        // tefBAD_LEDGER - unknown how to test/handle
        // tecINTERNAL - unknown how to test/handle
        // tecINTERNAL - unknown how to test/handle
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testEnabled(features);
        testMintInvalid(features);
        testBurnInvalid(features);
        testSellInvalid(features);
        testBuyInvalid(features);
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

BEAST_DEFINE_TESTSUITE(URIToken, app, ripple);
}  // namespace test
}  // namespace ripple
