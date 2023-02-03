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

    static std::pair<uint256, std::shared_ptr<SLE const>>
    uriTokenKeyAndSle(
        ReadView const& view,
        jtx::Account const& account,
        std::string const& uri)
    {
        auto const k = keylet::uritoken(account, Blob(uri.begin(), uri.end()));
        return {k.key, view.read(k)};
    }

    static bool
    uritExists(ReadView const& view, uint256 const& id)
    {
        auto const slep = view.read({ltURI_TOKEN, id});
        return bool(slep);
    }

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
    debugBalance(
        jtx::Env const& env,
        std::string const& name,
        jtx::Account const& account,
        jtx::IOU const& iou)
    {
        std::cout << name << " BALANCE XRP: " << env.balance(account) << "\n";
        std::cout << name << " BALANCE USD: " << env.balance(account, iou.issue()) << "\n";
    }

    // void
    // debugOwnerDir(
    //     jtx::Env const& env,
    //     std::string const& name,
    //     jtx::Account const& account,
    //     std::string const& uri)
    // {
    //     auto const [urit, uritSle] = uriTokenKeyAndSle(env.current(), account, uri);
    //     std::cout << "URIT: " << urit << "\n";
    //     std::cout << name << "IN OWNER DIR: " << inOwnerDir(env.current(), account, uritSle) << "\n";
    //     std::cout << name << "DIR: " << ownerDirCount(env.current(), account) << "\n";
    // }

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
        auto const dave = Account("dave");
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

        // clear sell
        env(clear(alice, id));
        env.close();

        // tecNO_PERMISSION - not listed
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

        // clear sell and set xrp sell
        env(clear(alice, id));
        env(sell(alice, id, XRP(1000)), txflags(tfSell));
        env.close();

        // tecINSUFFICIENT_PAYMENT - insuficient xrp sent
        env(buy(bob, id, XRP(900)), ter(tecINSUFFICIENT_PAYMENT));
        env.close();
        // tecINSUFFICIENT_FUNDS - insuficient xrp - fees
        env(buy(bob, id, XRP(1000)), ter(tecINSUFFICIENT_FUNDS));
        env.close();

        // clear sell and set usd sell
        env(clear(alice, id));
        env(sell(alice, id, USD(1000)), txflags(tfSell));
        env.close();

        // tecINSUFFICIENT_PAYMENT - insuficient amount sent
        env(buy(bob, id, USD(900)), ter(tecINSUFFICIENT_PAYMENT));
        env.close();

        // tecINSUFFICIENT_FUNDS - insuficient amount sent
        env(buy(bob, id, USD(10000)), ter(tecINSUFFICIENT_FUNDS));
        env.close();

        // fund dave 200 xrp (not enough for reserve)
        env.fund(XRP(260), dave);
        env.trust(USD(10000), dave);
        env(pay(gw, dave, USD(1000)));
        env.close();

        // auto const reserveFee = env.current()->fees().accountReserve(ownerDirCount(*env.current(), dave));
        // std::cout << "XRP RESERVE: " << reserveFee << "\n";
        // tecNO_LINE_INSUF_RESERVE - insuficient xrp to create line
        // env(buy(dave, id, USD(1000)), ter(tecNO_LINE_INSUF_RESERVE));
        // env.close();

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
    testClearInvalid(FeatureBitset features)
    {
        testcase("clear_invalid");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // setup env
        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];
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

        // temMALFORMED - invalid buy flag
        env(clear(alice, id), txflags(0b000100011U), ter(temMALFORMED));
        env.close();

        //----------------------------------------------------------------------
        // preclaim

        // tecNO_PERMISSION - not your uritoken
        env(clear(bob, id), ter(tecNO_PERMISSION));
        env.close();
    }

    void
    testMetaAndOwnership(FeatureBitset features)
    {
        testcase("Metadata & Ownership");

        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];

        std::string const uri(maxTokenURILength, '?');
        std::string const id{strHex(tokenid(alice, uri))};

        {
            // Test without adding the uritoken to the recipient's owner
            // directory
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, gw);
            env.trust(USD(10000), alice);
            env.trust(USD(10000), bob);
            env.close();
            env(pay(gw, alice, USD(1000)));
            env(pay(gw, bob, USD(1000)));
            env.close();

            env(mint(alice, uri));
            env(sell(alice, id, USD(10)), txflags(tfSell));
            env.close();
            auto const [urit, uritSle] = uriTokenKeyAndSle(*env.current(), alice, uri);
            BEAST_EXPECT(inOwnerDir(*env.current(), alice, uritSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 2);
            BEAST_EXPECT(!inOwnerDir(*env.current(), bob, uritSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 1);
            // // alice sets the sell offer
            // // bob sets the buy offer
            env(buy(bob, id, USD(10)));
            BEAST_EXPECT(uritExists(*env.current(), urit));
            BEAST_EXPECT(!inOwnerDir(*env.current(), alice, uritSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 1);
            BEAST_EXPECT(!inOwnerDir(*env.current(), bob, uritSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 2);
        }
        {
            // Test with adding the uritoken to the recipient's owner
            // directory
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, gw);
            env.trust(USD(10000), alice);
            env.trust(USD(10000), bob);
            env.close();
            env(pay(gw, alice, USD(1000)));
            env(pay(gw, bob, USD(1000)));
            env.close();

            env(mint(alice, uri));
            env(sell(alice, id, USD(10)), txflags(tfSell));
            env.close();
            auto const [urit, uritSle] = uriTokenKeyAndSle(*env.current(), alice, uri);
            BEAST_EXPECT(inOwnerDir(*env.current(), alice, uritSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 2);
            BEAST_EXPECT(!inOwnerDir(*env.current(), bob, uritSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 1);
            // // alice sets the sell offer
            // // bob sets the buy offer
            env(buy(bob, id, USD(10)));
            env.close();
            BEAST_EXPECT(uritExists(*env.current(), urit));
            BEAST_EXPECT(!inOwnerDir(*env.current(), alice, uritSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 1);
            BEAST_EXPECT(!inOwnerDir(*env.current(), bob, uritSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 2);
        }
    }

    void
    testAccountDelete(FeatureBitset features)
    {
        testcase("Account Delete");
        using namespace test::jtx;
        using namespace std::literals::chrono_literals;
        auto rmAccount = [this](
                             Env& env,
                             Account const& toRm,
                             Account const& dst,
                             TER expectedTer = tesSUCCESS) {
            // only allow an account to be deleted if the account's sequence
            // number is at least 256 less than the current ledger sequence
            for (auto minRmSeq = env.seq(toRm) + 257;
                 env.current()->seq() < minRmSeq;
                 env.close())
            {
            }

            env(acctdelete(toRm, dst),
                fee(drops(env.current()->fees().increment)),
                ter(expectedTer));
            env.close();
            this->BEAST_EXPECT(
                isTesSuccess(expectedTer) ==
                !env.closed()->exists(keylet::account(toRm.id())));
        };

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];

        std::string const uri(maxTokenURILength, '?');
        std::string const id{strHex(tokenid(alice, uri))};

        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol, gw);
            env.trust(USD(10000), alice);
            env.trust(USD(10000), bob);
            env.trust(USD(10000), carol);
            env.close();
            env(pay(gw, alice, USD(1000)));
            env(pay(gw, bob, USD(1000)));
            env(pay(gw, carol, USD(1000)));
            env.close();

            auto const feeDrops = env.current()->fees().base;

            // debugBalance(env, "alice", alice, USD);
            // debugBalance(env, "bob", bob, USD);

            // mint a uritoken from alice
            env(mint(alice, uri));
            env.close();
            BEAST_EXPECT(uritExists(*env.current(), tokenid(alice, uri)));
            env(sell(alice, id, USD(10)), txflags(tfSell));
            env.close();

            // alice has trustline + mint + sell
            rmAccount(env, alice, bob, tecHAS_OBLIGATIONS);

            env(clear(alice, id));
            env(burn(alice, id), txflags(tfBurn));
            env.close();

            // alice still has a trustline
            rmAccount(env, alice, bob, tecHAS_OBLIGATIONS);

            BEAST_EXPECT(uritExists(*env.current(), tokenid(alice, uri)));

            // buy should fail if the uri token was removed
            auto preBob = env.balance(bob, USD.issue());
            env(buy(bob, id, USD(10)), ter(tecNO_ENTRY));
            env.close();
            BEAST_EXPECT(env.balance(bob, USD.issue()) == preBob);
            
            env(mint(bob, uri));
            BEAST_EXPECT(uritExists(*env.current(), tokenid(bob, uri)));
        }
    }

    void
    testUsingTickets(FeatureBitset features)
    {
        testcase("using tickets");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto USD = gw["USD"];
        env.fund(XRP(1000), alice, bob, gw);
        env.trust(USD(10000), alice);
        env.trust(USD(10000), bob);
        env.close();
        env(pay(gw, alice, USD(1000)));
        env(pay(gw, bob, USD(1000)));
        env.close();

        // alice and bob grab enough tickets for all of the following
        // transactions.  Note that once the tickets are acquired alice's
        // and bob's account sequence numbers should not advance.
        std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
        env(ticket::create(alice, 10));
        std::uint32_t const aliceSeq{env.seq(alice)};

        std::uint32_t bobTicketSeq{env.seq(bob) + 1};
        env(ticket::create(bob, 10));
        std::uint32_t const bobSeq{env.seq(bob)};

        std::string const uri(maxTokenURILength, '?');
        std::string const id{strHex(tokenid(alice, uri))};

        env(mint(alice, uri), ticket::use(aliceTicketSeq++));

        env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        BEAST_EXPECT(uritExists(*env.current(), tokenid(alice, uri)));

        // {
        //     auto const preAlice = env.balance(alice);
        //     env(sell(alice, id, XRP(1000)), ticket::use(aliceTicketSeq++));

        //     env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        //     BEAST_EXPECT(env.seq(alice) == aliceSeq);

        //     auto const feeDrops = env.current()->fees().base;
        //     BEAST_EXPECT(env.balance(alice) == preAlice - XRP(1000) - feeDrops);
        // }

        // BEAST_EXPECT(uritExists(*env.current(), tokenid(alice, uri)));

        // {
        //     // No signature needed since the bob is buying
        //     auto const preBob = env.balance(bob);
        //     env(buy(bob, id, USD(10)), ticket::use(bobTicketSeq++));

        //     env.require(tickets(bob, env.seq(bob) - bobTicketSeq));
        //     BEAST_EXPECT(env.seq(bob) == bobSeq);

        //     BEAST_EXPECT(uritExists(*env.current(), tokenid(alice, uri)));
        //     BEAST_EXPECT(env.balance(bob) == preBob + USD(10));
        // }
        // {
        //     // Claim with signature
        //     auto preBob = env.balance(bob);
        //     auto const delta = XRP(500);
        //     auto const reqBal = chanBal + delta;
        //     auto const authAmt = reqBal + XRP(100);
        //     assert(reqBal <= chanAmt);
        //     auto const sig =
        //         signClaimAuth(alice.pk(), alice.sk(), chan, authAmt);
        //     env(claim(bob, chan, reqBal, authAmt, Slice(sig), alice.pk()),
        //         ticket::use(bobTicketSeq++));

        //     env.require(tickets(bob, env.seq(bob) - bobTicketSeq));
        //     BEAST_EXPECT(env.seq(bob) == bobSeq);

        //     BEAST_EXPECT(channelBalance(*env.current(), chan) == reqBal);
        //     BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
        //     auto const feeDrops = env.current()->fees().base;
        //     BEAST_EXPECT(env.balance(bob) == preBob + delta - feeDrops);
        //     chanBal = reqBal;

        //     // claim again
        //     preBob = env.balance(bob);
        //     // A transaction that generates a tec still consumes its ticket.
        //     env(claim(bob, chan, reqBal, authAmt, Slice(sig), alice.pk()),
        //         ticket::use(bobTicketSeq++),
        //         ter(tecUNFUNDED_PAYMENT));

        //     env.require(tickets(bob, env.seq(bob) - bobTicketSeq));
        //     BEAST_EXPECT(env.seq(bob) == bobSeq);

        //     BEAST_EXPECT(channelBalance(*env.current(), chan) == chanBal);
        //     BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
        //     BEAST_EXPECT(env.balance(bob) == preBob - feeDrops);
        // }
        // {
        //     // Try to claim more than authorized
        //     auto const preBob = env.balance(bob);
        //     STAmount const authAmt = chanBal + XRP(500);
        //     STAmount const reqAmt = authAmt + drops(1);
        //     assert(reqAmt <= chanAmt);
        //     // Note that since claim() returns a tem (neither tec nor tes),
        //     // the ticket is not consumed.  So we don't increment bobTicket.
        //     auto const sig =
        //         signClaimAuth(alice.pk(), alice.sk(), chan, authAmt);
        //     env(claim(bob, chan, reqAmt, authAmt, Slice(sig), alice.pk()),
        //         ticket::use(bobTicketSeq),
        //         ter(temBAD_AMOUNT));

        //     env.require(tickets(bob, env.seq(bob) - bobTicketSeq));
        //     BEAST_EXPECT(env.seq(bob) == bobSeq);

        //     BEAST_EXPECT(channelBalance(*env.current(), chan) == chanBal);
        //     BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
        //     BEAST_EXPECT(env.balance(bob) == preBob);
        // }

        // // Dst tries to fund the channel
        // env(fund(bob, chan, XRP(1000)),
        //     ticket::use(bobTicketSeq++),
        //     ter(tecNO_PERMISSION));

        // env.require(tickets(bob, env.seq(bob) - bobTicketSeq));
        // BEAST_EXPECT(env.seq(bob) == bobSeq);

        // BEAST_EXPECT(channelBalance(*env.current(), chan) == chanBal);
        // BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);

        // {
        //     // Dst closes channel
        //     auto const preAlice = env.balance(alice);
        //     auto const preBob = env.balance(bob);
        //     env(claim(bob, chan),
        //         txflags(tfClose),
        //         ticket::use(bobTicketSeq++));

        //     env.require(tickets(bob, env.seq(bob) - bobTicketSeq));
        //     BEAST_EXPECT(env.seq(bob) == bobSeq);

        //     BEAST_EXPECT(!channelExists(*env.current(), chan));
        //     auto const feeDrops = env.current()->fees().base;
        //     auto const delta = chanAmt - chanBal;
        //     assert(delta > beast::zero);
        //     BEAST_EXPECT(env.balance(alice) == preAlice + delta);
        //     BEAST_EXPECT(env.balance(bob) == preBob - feeDrops);
        // }
        // env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        // BEAST_EXPECT(env.seq(alice) == aliceSeq);
        // env.require(tickets(bob, env.seq(bob) - bobTicketSeq));
        // BEAST_EXPECT(env.seq(bob) == bobSeq);
    }

    void
    testWithFeats(FeatureBitset features)
    {
        // testEnabled(features);
        // testMintInvalid(features);
        // testBurnInvalid(features);
        // testSellInvalid(features);
        // testBuyInvalid(features);
        // testClearInvalid(features);
        // testMetaAndOwnership(features);
        // testAccountDelete(features);
        testUsingTickets(features);
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
