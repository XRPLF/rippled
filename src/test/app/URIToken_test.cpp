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
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>

#include <chrono>

namespace ripple {
namespace test {
struct URIToken_test : public beast::unit_test::suite
{
    static uint256
    tokenid(jtx::Account const& account, std::string const& uri)
    {
        auto const k = keylet::uritoken(account, Blob(uri.begin(), uri.end()));
        return k.key;
    }

    static bool
    inOwnerDir(
        ReadView const& view,
        jtx::Account const& acct,
        uint256 const& tid)
    {
        auto const uritSle = view.read({ltURI_TOKEN, tid});
        ripple::Dir const ownerDir(view, keylet::ownerDir(acct.id()));
        return std::find(ownerDir.begin(), ownerDir.end(), uritSle) !=
            ownerDir.end();
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

    static STAmount
    limitAmount(
        jtx::Env const& env,
        jtx::Account const& account,
        jtx::Account const& gw,
        jtx::IOU const& iou)
    {
        auto const aHigh = account.id() > gw.id();
        auto const sle = env.le(keylet::line(account, gw, iou.currency));
        if (sle && sle->isFieldPresent(aHigh ? sfLowLimit : sfHighLimit))
            return (*sle)[aHigh ? sfLowLimit : sfHighLimit];
        return STAmount(iou, 0);
    }

    static AccountID
    tokenOwner(ReadView const& view, uint256 const& id)
    {
        auto const slep = view.read({ltURI_TOKEN, id});
        if (!slep)
            return AccountID();
        return slep->getAccountID(sfOwner);
    }

    static uint256
    tokenDigest(ReadView const& view, uint256 const& id)
    {
        auto const slep = view.read({ltURI_TOKEN, id});
        if (!slep)
            return uint256{0};
        return slep->getFieldH256(sfDigest);
    }

    static STAmount
    tokenAmount(ReadView const& view, uint256 const& id)
    {
        auto const slep = view.read({ltURI_TOKEN, id});
        if (!slep)
            return XRPAmount{-1};
        if (slep->getFieldAmount(sfAmount))
            return (*slep)[sfAmount];
        return XRPAmount{-1};
    }

    static STAmount
    lineBalance(
        jtx::Env const& env,
        jtx::Account const& account,
        jtx::Account const& gw,
        jtx::IOU const& iou)
    {
        auto const sle = env.le(keylet::line(account, gw, iou.currency));
        if (sle && sle->isFieldPresent(sfBalance))
            return (*sle)[sfBalance];
        return STAmount(iou, 0);
    }

    static Json::Value
    mint(jtx::Account const& account, std::string const& uri)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::TransactionType] = jss::URITokenMint;
        jv[jss::Account] = account.human();
        jv[sfURI.jsonName] = strHex(uri);
        return jv;
    }

    static Json::Value
    burn(jtx::Account const& account, std::string const& id)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::TransactionType] = jss::URITokenBurn;
        jv[jss::Account] = account.human();
        jv[sfURITokenID.jsonName] = id;
        return jv;
    }

    static Json::Value
    buy(jtx::Account const& account,
        std::string const& id,
        STAmount const& amount)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::TransactionType] = jss::URITokenBuy;
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
        jv[jss::TransactionType] = jss::URITokenCreateSellOffer;
        jv[jss::Account] = account.human();
        jv[jss::Amount] = amount.getJson(JsonOptions::none);
        jv[sfURITokenID.jsonName] = id;
        return jv;
    }

    static Json::Value
    clear(jtx::Account const& account, std::string const& id)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::TransactionType] = jss::URITokenCancelSellOffer;
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

        for (bool const withURIToken : {false, true})
        {
            // If the URIToken amendment is not enabled, you should not be able
            // to mint, burn, buy, sell or clear uri tokens.
            auto const amend =
                withURIToken ? features : features - featureURIToken;
            Env env{*this, amend};

            env.fund(XRP(1000), alice, bob);
            env.close();

            std::string const uri(maxTokenURILength, '?');
            std::string const id{strHex(tokenid(alice, uri))};

            auto const txResult =
                withURIToken ? ter(tesSUCCESS) : ter(temDISABLED);
            auto const ownerDir = withURIToken ? 1 : 0;

            // MINT
            env(mint(alice, uri), txResult);
            env.close();
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == ownerDir);
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 0);

            // SELL
            env(sell(alice, id, XRP(10)), txResult);
            env.close();
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == ownerDir);
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 0);

            // BUY
            env(buy(bob, id, XRP(10)), txResult);
            env.close();
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 0);
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == ownerDir);

            // SELL
            env(sell(bob, id, XRP(10)), txResult);
            env.close();
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 0);
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == ownerDir);

            // CLEAR
            env(clear(bob, id), txResult);
            env.close();
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 0);
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == ownerDir);

            // BURN
            env(burn(bob, id), txResult);
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
        auto const bob = Account("bob");

        env.fund(XRP(200), alice);
        env.close();

        std::string const uri(2, '?');
        auto const tid = tokenid(alice, uri);
        std::string const hexid{strHex(tid)};

        //----------------------------------------------------------------------
        // preflight

        {
            // temINVALID_FLAG - invalid flags
            env(mint(alice, uri), txflags(tfAllowXRP), ter(temINVALID_FLAG));
            env.close();

            // temMALFORMED - no uri & no flags
            std::string const nouri(0, '?');
            env(mint(alice, nouri), ter(temMALFORMED));
            env.close();

            // temMALFORMED - bad uri 257 len
            std::string const longuri(maxTokenURILength + 1, '?');
            env(mint(alice, longuri), ter(temMALFORMED));
            env.close();
        }

        //----------------------------------------------------------------------
        // preclaim

        {
            env.fund(XRP(251), bob);
            env.close();
            auto const btid = tokenid(bob, uri);
            std::string const bhexid{strHex(btid)};
            // tecDUPLICATE - duplicate uri token
            env(mint(bob, uri), txflags(tfBurnable));
            env(mint(bob, uri), ter(tecDUPLICATE));
            env(burn(bob, bhexid));
            env.close();
        }

        //----------------------------------------------------------------------
        // doApply

        {
            // tecINSUFFICIENT_RESERVE - out of xrp
            env(mint(alice, uri), ter(tecINSUFFICIENT_RESERVE));
            env.close();

            // tecDIR_FULL - directory full
        }
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
        auto const tid = tokenid(alice, uri);
        std::string const hexid{strHex(tid)};
        env(mint(alice, uri));
        env.close();

        // temINVALID_FLAG - invalid flags
        env(burn(alice, hexid), txflags(tfAllowXRP), ter(temINVALID_FLAG));
        env.close();

        //----------------------------------------------------------------------
        // preclaim

        // tecNO_ENTRY - no exists item
        std::string const neuri(3, '?');
        auto const netid = tokenid(alice, neuri);
        std::string const hexneuri{strHex(netid)};
        env(burn(alice, hexneuri), ter(tecNO_ENTRY));
        env.close();

        // tecNO_ENTRY - no owner exists
        // impossible test

        // tecNO_PERMISSION - not owner and not (issuer/burnable)
        env(burn(bob, hexid), ter(tecNO_PERMISSION));
        env.close();

        //----------------------------------------------------------------------
        // doApply

        // tecNO_PERMISSION - no permission
        env(burn(carol, hexid), ter(tecNO_PERMISSION));
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
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const ngw = Account{"ngateway"};
        auto const USD = gw["USD"];
        auto const NUSD = ngw["USD"];
        env.fund(XRP(1000), alice, bob, gw);
        env.close();
        env.trust(USD(100000), alice, bob);
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
        // preflight

        // temBAD_AMOUNT - bad xrp/amount
        env(sell(alice, id, XRP(-1)), ter(temBAD_AMOUNT));
        env.close();

        // temBAD_AMOUNT - bad ft/amount
        env(sell(alice, id, USD(-1)), ter(temBAD_AMOUNT));
        env.close();

        // temBAD_CURRENCY - bad currency
        IOU const BAD{gw, badCurrency()};
        env(sell(alice, id, BAD(10)), ter(temBAD_CURRENCY));

        // temMALFORMED - no destination and 0 value
        env(sell(alice, id, USD(0)), ter(temMALFORMED));
        env.close();

        //----------------------------------------------------------------------
        // preclaim
        // tecNO_PERMISSION - invalid account
        env(sell(bob, id, USD(10)), ter(tecNO_PERMISSION));
        env.close();

        // tecNO_ISSUER - invalid issuer
        env(sell(alice, id, NUSD(10)), ter(tecNO_ISSUER));
        env.close();

        //----------------------------------------------------------------------
        // doApply

        // tecNO_PERMISSION - invalid account
        env(sell(bob, id, USD(10)), ter(tecNO_PERMISSION));
        env.close();
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
        auto const echo = Account("echo");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];
        env.fund(XRP(1000), alice, bob, carol, gw);
        env.trust(USD(100000), alice, bob, carol);
        env.close();
        env(pay(gw, alice, USD(1000)));
        env(pay(gw, bob, USD(1000)));
        env(pay(gw, carol, USD(1000)));
        env.close();

        // mint token
        std::string const uri(2, '?');
        auto const tid = tokenid(alice, uri);
        std::string const hexid{strHex(tid)};
        env(mint(alice, uri));
        env.close();

        //----------------------------------------------------------------------
        // preclaim

        // tecNO_PERMISSION - not for sale
        env(buy(bob, hexid, USD(10)), ter(tecNO_PERMISSION));
        env.close();

        // set sell
        env(sell(alice, hexid, USD(10)), jtx::token::destination(bob));
        env.close();

        // tecNO_PERMISSION - for sale to dest, you are not dest
        env(buy(carol, hexid, USD(10)), ter(tecNO_PERMISSION));
        env.close();

        // temBAD_CURRENCY - invalid buy sell amounts
        env(buy(bob, hexid, EUR(10)), ter(temBAD_CURRENCY));
        env.close();

        // tecINSUFFICIENT_PAYMENT - insuficient buy offer amount
        env(buy(bob, hexid, USD(9)), ter(tecINSUFFICIENT_PAYMENT));
        env.close();

        env(clear(alice, hexid));
        env(sell(alice, hexid, XRP(10000)));
        env.close();

        // tecINSUFFICIENT_FUNDS - insuficient xrp - fees
        env(buy(bob, hexid, XRP(1000)), ter(tecINSUFFICIENT_PAYMENT));
        env.close();

        // clear sell and reset new sell
        env(clear(alice, hexid));
        env(sell(alice, hexid, USD(10000)));
        env.close();

        // tecINSUFFICIENT_FUNDS - insuficient amount
        env(buy(bob, hexid, USD(1000)), ter(tecINSUFFICIENT_PAYMENT));
        env.close();

        //----------------------------------------------------------------------
        // doApply

        // clear sell
        env(clear(alice, hexid));
        env.close();

        // tecNO_PERMISSION - not listed
        env(buy(bob, hexid, USD(10)), ter(tecNO_PERMISSION));
        env.close();

        // set sell
        env(sell(alice, hexid, USD(10)), jtx::token::destination(bob));
        env.close();

        // tecNO_PERMISSION - for sale to dest, you are not dest
        env(buy(carol, hexid, USD(10)), ter(tecNO_PERMISSION));
        env.close();

        // temBAD_CURRENCY - invalid buy sell amounts
        env(buy(bob, hexid, EUR(10)), ter(temBAD_CURRENCY));
        env.close();

        // clear sell and set xrp sell
        env(clear(alice, hexid));
        env(sell(alice, hexid, XRP(1000)));
        env.close();

        // tecINSUFFICIENT_PAYMENT - insuficient xrp sent
        env(buy(bob, hexid, XRP(900)), ter(tecINSUFFICIENT_PAYMENT));
        env.close();
        // tecINSUFFICIENT_FUNDS - insuficient xrp - fees
        env(buy(bob, hexid, XRP(1000)), ter(tecINSUFFICIENT_FUNDS));
        env.close();

        // clear sell and set usd sell
        env(clear(alice, hexid));
        env(sell(alice, hexid, USD(1000)));
        env.close();

        // tecINSUFFICIENT_PAYMENT - insuficient amount sent
        env(buy(bob, hexid, USD(900)), ter(tecINSUFFICIENT_PAYMENT));
        env.close();

        // tecINSUFFICIENT_FUNDS - insuficient amount sent
        env(buy(bob, hexid, USD(10000)), ter(tecINSUFFICIENT_FUNDS));
        env.close();
        // tecNO_LINE_INSUF_RESERVE - insuficient xrp to create line
        {
            // fund dave 251 xrp (not enough for line reserve)
            env.fund(XRP(251), echo);
            env.fund(XRP(301), dave);
            env.close();
            env.trust(USD(100000), dave);
            env.close();
            env(pay(gw, dave, USD(1000)));
            env.close();

            // mint token
            std::string const uri(3, '?');
            auto const tid = tokenid(echo, uri);
            std::string const hexid{strHex(tid)};
            env(mint(echo, uri));
            env(sell(echo, hexid, USD(1)));
            env.close();

            // tecNO_LINE_INSUF_RESERVE - insuficient xrp to create line
            env(buy(dave, hexid, USD(1)), ter(tecNO_LINE_INSUF_RESERVE));
            env.close();
        }

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
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];
        env.fund(XRP(1000), alice, bob, gw);
        env.trust(USD(100000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(1000)));
        env(pay(gw, bob, USD(1000)));
        env.close();

        // mint token
        std::string const uri(2, '?');
        auto const tid = tokenid(alice, uri);
        std::string const hexid{strHex(tid)};
        env(mint(alice, uri));
        env.close();

        //----------------------------------------------------------------------
        // operator preflight
        // temDISABLED

        // temINVALID_FLAG - invalid flag
        env(clear(alice, hexid), txflags(tfAllowXRP), ter(temINVALID_FLAG));
        env.close();

        //----------------------------------------------------------------------
        // preclaim

        // tecNO_PERMISSION - not your uritoken
        env(clear(bob, hexid), ter(tecNO_PERMISSION));
        env.close();
    }

    void
    testMintValid(FeatureBitset features)
    {
        testcase("mint");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        // setup env
        Env env{*this, features};
        env.fund(XRP(1000), alice, bob);

        std::string const uri(maxTokenURILength, '?');
        auto const tid = tokenid(alice, uri);
        std::string const hexid{strHex(tid)};

        std::string const digestval =
            "C16E7263F07AA41261DCC955660AF4646ADBA414E37B6F5A5BA50F75153F5CCC";

        // has digest - has uri - no flags
        {
            // mint
            env(mint(alice, uri), json(sfDigest.fieldName, digestval));
            env.close();
            BEAST_EXPECT(inOwnerDir(*env.current(), alice, tid));
            BEAST_EXPECT(
                to_string(tokenDigest(*env.current(), tid)) == digestval);
            // cleanup
            env(burn(alice, hexid));
            env.close();
            BEAST_EXPECT(!inOwnerDir(*env.current(), alice, tid));
        }
        // has digest - has uri - burnable flag
        {
            // mint
            env(mint(alice, uri),
                txflags(tfBurnable),
                json(sfDigest.fieldName, digestval));
            env.close();
            BEAST_EXPECT(inOwnerDir(*env.current(), alice, tid));
            BEAST_EXPECT(
                to_string(tokenDigest(*env.current(), tid)) == digestval);
            // cleanup
            env(burn(alice, hexid));
            env.close();
            BEAST_EXPECT(!inOwnerDir(*env.current(), alice, tid));
        }
        // has uri - no flags
        {
            // mint
            env(mint(alice, uri));
            env.close();
            BEAST_EXPECT(inOwnerDir(*env.current(), alice, tid));
            // cleanup
            env(burn(alice, hexid));
            env.close();
            BEAST_EXPECT(!inOwnerDir(*env.current(), alice, tid));
        }
        // has uri - burnable flag
        {
            // mint
            env(mint(alice, uri), txflags(tfBurnable));
            env.close();
            BEAST_EXPECT(inOwnerDir(*env.current(), alice, tid));
            // cleanup
            env(burn(alice, hexid));
            env.close();
            BEAST_EXPECT(!inOwnerDir(*env.current(), alice, tid));
        }
    }

    void
    testBurnValid(FeatureBitset features)
    {
        testcase("burn");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        // setup env
        Env env{*this, features};
        env.fund(XRP(1000), alice, bob);

        std::string const uri(maxTokenURILength, '?');
        auto const tid = tokenid(alice, uri);
        std::string const hexid{strHex(tid)};

        // issuer can burn
        {
            // alice mints
            env(mint(alice, uri), txflags(tfBurnable));
            env.close();
            BEAST_EXPECT(inOwnerDir(*env.current(), alice, tid));
            // alice sells
            env(sell(alice, hexid, XRP(1)));
            env.close();
            // bob buys
            env(buy(bob, hexid, XRP(1)));
            env.close();
            // alice burns
            env(burn(alice, hexid));
            env.close();
            BEAST_EXPECT(!inOwnerDir(*env.current(), alice, tid));
        }
        // issuer cannot burn
        {
            // alice mints
            env(mint(alice, uri));
            env.close();
            BEAST_EXPECT(inOwnerDir(*env.current(), alice, tid));
            // alice sells
            env(sell(alice, hexid, XRP(1)));
            env.close();
            // bob buys
            env(buy(bob, hexid, XRP(1)));
            env.close();
            BEAST_EXPECT(inOwnerDir(*env.current(), bob, tid));
            // alice tries to burn
            env(burn(alice, hexid), ter(tecNO_PERMISSION));
            env.close();
            BEAST_EXPECT(inOwnerDir(*env.current(), bob, tid));
            // burn for test reset
            env(burn(bob, hexid));
            env.close();
            BEAST_EXPECT(!inOwnerDir(*env.current(), bob, tid));
        }
        // owner can burn
        {
            // alice mints
            env(mint(alice, uri));
            env.close();
            BEAST_EXPECT(inOwnerDir(*env.current(), alice, tid));
            // alice sells
            env(sell(alice, hexid, XRP(1)));
            env.close();
            // bob buys
            env(buy(bob, hexid, XRP(1)));
            env.close();
            BEAST_EXPECT(inOwnerDir(*env.current(), bob, tid));
            // bob burns
            env(burn(bob, hexid));
            env.close();
            BEAST_EXPECT(!inOwnerDir(*env.current(), alice, tid));
            BEAST_EXPECT(!inOwnerDir(*env.current(), bob, tid));
        }
    }

    void
    testBuyValid(FeatureBitset features)
    {
        testcase("buy");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Env env{*this, features};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        // setup env
        env.fund(XRP(1000), alice, bob, gw);
        env.trust(USD(100000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(1000)));
        env(pay(gw, bob, USD(1000)));
        env.close();

        auto const feeDrops = env.current()->fees().base;
        std::string const uri(maxTokenURILength, '?');
        auto const tid = tokenid(alice, uri);
        std::string const hexid{strHex(tid)};

        // bob can buy with XRP
        {
            // alice mints
            const auto delta = XRP(10);
            auto preAlice = env.balance(alice);
            auto preBob = env.balance(bob);
            env(mint(alice, uri));
            env.close();
            BEAST_EXPECT(inOwnerDir(*env.current(), alice, tid));
            BEAST_EXPECT(env.balance(alice) == preAlice - (1 * feeDrops));
            // alice sells
            env(sell(alice, hexid, delta));
            BEAST_EXPECT(env.balance(alice) == preAlice - (2 * feeDrops));
            env.close();
            // bob buys
            env(buy(bob, hexid, delta));
            env.close();

            BEAST_EXPECT(inOwnerDir(*env.current(), bob, tid));
            BEAST_EXPECT(
                env.balance(alice) == preAlice + delta - (2 * feeDrops));
            BEAST_EXPECT(env.balance(bob) == preBob - delta - feeDrops);
            BEAST_EXPECT(bob.id() == tokenOwner(*env.current(), tid));

            // bob burns to reset tests
            env(burn(bob, hexid));
            env.close();
            BEAST_EXPECT(!inOwnerDir(*env.current(), alice, tid));
            BEAST_EXPECT(!inOwnerDir(*env.current(), bob, tid));
        }
        // bob can buy with USD
        {
            // alice mints
            const auto delta = USD(10);
            auto preAlice = env.balance(alice, USD.issue());
            auto preAliceXrp = env.balance(alice);
            auto preBob = env.balance(bob, USD.issue());
            auto preBobXrp = env.balance(bob);
            env(mint(alice, uri));
            env.close();
            BEAST_EXPECT(inOwnerDir(*env.current(), alice, tid));
            BEAST_EXPECT(env.balance(alice) == preAliceXrp - (1 * feeDrops));
            // alice sells
            env(sell(alice, hexid, delta));
            BEAST_EXPECT(env.balance(alice) == preAliceXrp - (2 * feeDrops));
            BEAST_EXPECT(env.balance(alice, USD.issue()) == preAlice);
            env.close();
            // bob buys
            env(buy(bob, hexid, delta));
            env.close();
            BEAST_EXPECT(inOwnerDir(*env.current(), bob, tid));
            BEAST_EXPECT(env.balance(alice, USD.issue()) == preAlice + delta);
            BEAST_EXPECT(env.balance(alice) == preAliceXrp - (2 * feeDrops));
            BEAST_EXPECT(env.balance(bob, USD.issue()) == preBob - delta);
            BEAST_EXPECT(env.balance(bob) == preBobXrp - (1 * feeDrops));
            BEAST_EXPECT(bob.id() == tokenOwner(*env.current(), tid));

            // bob burns to reset tests
            env(burn(bob, hexid));
            env.close();
            BEAST_EXPECT(!inOwnerDir(*env.current(), alice, tid));
            BEAST_EXPECT(!inOwnerDir(*env.current(), bob, tid));
        }
    }

    void
    testSellValid(FeatureBitset features)
    {
        testcase("sell");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        // setup env
        Env env{*this, features};
        env.fund(XRP(1000), alice, bob, carol, gw);
        env.close();
        env.trust(USD(100000), alice, bob, carol);
        env.close();
        env(pay(gw, alice, USD(1000)));
        env(pay(gw, bob, USD(1000)));
        env(pay(gw, carol, USD(1000)));
        env.close();

        auto const feeDrops = env.current()->fees().base;
        std::string const uri(maxTokenURILength, '?');
        auto const tid = tokenid(alice, uri);
        std::string const hexid{strHex(tid)};

        // alice can sell with XRP
        {
            // alice mints
            const auto delta = XRP(10);
            auto preAlice = env.balance(alice);
            auto preBob = env.balance(bob);
            env(mint(alice, uri));
            env.close();
            BEAST_EXPECT(inOwnerDir(*env.current(), alice, tid));
            // alice sells
            env(sell(alice, hexid, delta));
            env.close();
            BEAST_EXPECT(delta == tokenAmount(*env.current(), tid));
            // alice clears and sells again at a higher price
            env(clear(alice, hexid));
            BEAST_EXPECT(XRPAmount{-1} == tokenAmount(*env.current(), tid));
            env(sell(alice, hexid, XRP(11)));
            env.close();
            BEAST_EXPECT(XRP(11) == tokenAmount(*env.current(), tid));
            // bob tries to buy at original price and fails
            env(buy(bob, hexid, delta), ter(tecINSUFFICIENT_PAYMENT));
            // bob buys at higher price
            env(buy(bob, hexid, XRP(11)));
            env.close();
            BEAST_EXPECT(inOwnerDir(*env.current(), bob, tid));
            BEAST_EXPECT(
                env.balance(alice) == preAlice + XRP(11) - (4 * feeDrops));
            BEAST_EXPECT(env.balance(bob) == preBob - XRP(11) - (2 * feeDrops));
            BEAST_EXPECT(bob.id() == tokenOwner(*env.current(), tid));

            // bob burns to reset tests
            env(burn(bob, hexid));
            env.close();
            BEAST_EXPECT(!inOwnerDir(*env.current(), alice, tid));
            BEAST_EXPECT(!inOwnerDir(*env.current(), bob, tid));
        }
        // alice can sell with XRP and dest
        {
            // alice mints
            const auto delta = XRP(10);
            auto preAlice = env.balance(alice);
            auto preBob = env.balance(bob);
            env(mint(alice, uri));
            env.close();
            BEAST_EXPECT(inOwnerDir(*env.current(), alice, tid));
            // alice sells
            env(sell(alice, hexid, delta), jtx::token::destination(bob));
            env.close();
            BEAST_EXPECT(delta == tokenAmount(*env.current(), tid));
            // carol tries to buy but cannot
            env(buy(carol, hexid, delta), ter(tecNO_PERMISSION));
            env.close();
            // bob buys
            env(buy(bob, hexid, delta));
            env.close();
            BEAST_EXPECT(inOwnerDir(*env.current(), bob, tid));
            BEAST_EXPECT(
                env.balance(alice) == preAlice + delta - (2 * feeDrops));
            BEAST_EXPECT(env.balance(bob) == preBob - delta - (1 * feeDrops));
            BEAST_EXPECT(bob.id() == tokenOwner(*env.current(), tid));

            // bob burns to reset tests
            env(burn(bob, hexid));
            env.close();
            BEAST_EXPECT(!inOwnerDir(*env.current(), alice, tid));
            BEAST_EXPECT(!inOwnerDir(*env.current(), bob, tid));
        }

        // alice can sell with USD
        {
            // alice mints
            const auto delta = USD(10);
            auto preAlice = env.balance(alice, USD.issue());
            auto preAliceXrp = env.balance(alice);
            auto preBob = env.balance(bob, USD.issue());
            auto preBobXrp = env.balance(bob);
            env(mint(alice, uri));
            env.close();
            BEAST_EXPECT(inOwnerDir(*env.current(), alice, tid));
            // alice sells
            env(sell(alice, hexid, delta));
            env.close();
            BEAST_EXPECT(delta == tokenAmount(*env.current(), tid));
            // alice clears and sells again at a higher price
            env(clear(alice, hexid));
            BEAST_EXPECT(XRPAmount{-1} == tokenAmount(*env.current(), tid));
            env(sell(alice, hexid, USD(11)));
            env.close();
            BEAST_EXPECT(USD(11) == tokenAmount(*env.current(), tid));
            // bob tries to buy at original price and fails
            env(buy(bob, hexid, delta), ter(tecINSUFFICIENT_PAYMENT));
            // bob buys at higher price
            env(buy(bob, hexid, USD(11)));
            env.close();

            BEAST_EXPECT(inOwnerDir(*env.current(), bob, tid));
            BEAST_EXPECT(env.balance(alice, USD.issue()) == preAlice + USD(11));
            BEAST_EXPECT(env.balance(alice) == preAliceXrp - (4 * feeDrops));
            BEAST_EXPECT(env.balance(bob, USD.issue()) == preBob - USD(11));
            BEAST_EXPECT(env.balance(bob) == preBobXrp - (2 * feeDrops));
            BEAST_EXPECT(bob.id() == tokenOwner(*env.current(), tid));

            // bob burns to reset tests
            env(burn(bob, hexid));
            env.close();
            BEAST_EXPECT(!inOwnerDir(*env.current(), alice, tid));
            BEAST_EXPECT(!inOwnerDir(*env.current(), bob, tid));
        }
        // alice can sell with USD and dest
        {
            // alice mints
            const auto delta = USD(10);
            auto preAlice = env.balance(alice, USD.issue());
            auto preAliceXrp = env.balance(alice);
            auto preBob = env.balance(bob, USD.issue());
            auto preBobXrp = env.balance(bob);
            env(mint(alice, uri));
            env.close();
            BEAST_EXPECT(inOwnerDir(*env.current(), alice, tid));
            // alice sells
            env(sell(alice, hexid, delta), jtx::token::destination(bob));
            env.close();
            BEAST_EXPECT(delta == tokenAmount(*env.current(), tid));
            // carol tries to buy but cannot
            env(buy(carol, hexid, delta), ter(tecNO_PERMISSION));
            env.close();
            // bob buys
            env(buy(bob, hexid, delta));
            env.close();

            BEAST_EXPECT(inOwnerDir(*env.current(), bob, tid));
            BEAST_EXPECT(env.balance(alice, USD.issue()) == preAlice + delta);
            BEAST_EXPECT(env.balance(alice) == preAliceXrp - (2 * feeDrops));
            BEAST_EXPECT(env.balance(bob, USD.issue()) == preBob - delta);
            BEAST_EXPECT(env.balance(bob) == preBobXrp - (1 * feeDrops));
            BEAST_EXPECT(bob.id() == tokenOwner(*env.current(), tid));

            // bob burns to reset tests
            env(burn(bob, hexid));
            env.close();
            BEAST_EXPECT(!inOwnerDir(*env.current(), alice, tid));
            BEAST_EXPECT(!inOwnerDir(*env.current(), bob, tid));
        }
    }

    void
    testClearValid(FeatureBitset features)
    {
        testcase("clear");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        // setup env
        Env env{*this, features};
        env.fund(XRP(1000), alice, bob, carol, gw);
        env.close();
        env.trust(USD(100000), alice, bob, carol);
        env.close();
        env(pay(gw, alice, USD(1000)));
        env(pay(gw, bob, USD(1000)));
        env(pay(gw, carol, USD(1000)));
        env.close();

        // auto const feeDrops = env.current()->fees().base;
        std::string const uri(maxTokenURILength, '?');
        auto const tid = tokenid(alice, uri);
        std::string const hexid{strHex(tid)};

        // alice can clear / reset XRP amount
        {
            // alice mints
            const auto delta = XRP(10);
            auto preAlice = env.balance(alice);
            env(mint(alice, uri));
            env.close();
            BEAST_EXPECT(inOwnerDir(*env.current(), alice, tid));
            // alice sells
            env(sell(alice, hexid, delta));
            env.close();
            BEAST_EXPECT(delta == tokenAmount(*env.current(), tid));
            // alice clears the sell amount
            env(clear(alice, hexid));
            BEAST_EXPECT(XRPAmount{-1} == tokenAmount(*env.current(), tid));
            // alice sets sell for higher amount
            env(sell(alice, hexid, XRP(11)));
            env.close();
            BEAST_EXPECT(XRP(11) == tokenAmount(*env.current(), tid));
            // alice clears the sell amount
            env(clear(alice, hexid));
            BEAST_EXPECT(XRPAmount{-1} == tokenAmount(*env.current(), tid));

            // alice burns to reset tests
            env(burn(alice, hexid));
            env.close();
            BEAST_EXPECT(!inOwnerDir(*env.current(), alice, tid));
        }
        // alice can clear / reset USD amount
        {
            // alice mints
            const auto delta = USD(10);
            env(mint(alice, uri));
            env.close();
            BEAST_EXPECT(inOwnerDir(*env.current(), alice, tid));
            // alice sells
            env(sell(alice, hexid, delta));
            env.close();
            BEAST_EXPECT(delta == tokenAmount(*env.current(), tid));
            // alice clears the sell amount
            env(clear(alice, hexid));
            BEAST_EXPECT(XRPAmount{-1} == tokenAmount(*env.current(), tid));
            // alice sets sell for higher amount
            env(sell(alice, hexid, USD(11)));
            env.close();
            BEAST_EXPECT(USD(11) == tokenAmount(*env.current(), tid));
            // alice clears the sell amount
            env(clear(alice, hexid));
            BEAST_EXPECT(XRPAmount{-1} == tokenAmount(*env.current(), tid));

            // alice burns to reset tests
            env(burn(alice, hexid));
            env.close();
            BEAST_EXPECT(!inOwnerDir(*env.current(), alice, tid));
        }
    }

    void
    testMetaAndOwnership(FeatureBitset features)
    {
        testcase("metadata_and_onwnership");

        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        std::string const uri(maxTokenURILength, '?');
        uint256 const tid = tokenid(alice, uri);
        std::string const hexid{strHex(tid)};

        {
            // Test without adding the uritoken to the recipient's owner
            // directory
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, gw);
            env.close();
            env.trust(USD(100000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(1000)));
            env(pay(gw, bob, USD(1000)));
            env.close();

            env(mint(alice, uri));
            env(sell(alice, hexid, USD(10)));
            env.close();
            BEAST_EXPECT(inOwnerDir(*env.current(), alice, tid));
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 2);
            BEAST_EXPECT(!inOwnerDir(*env.current(), bob, tid));
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 1);
            // // alice sets the sell offer
            // // bob sets the buy offer
            env(buy(bob, hexid, USD(10)));
            BEAST_EXPECT(!inOwnerDir(*env.current(), alice, tid));
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 1);
            BEAST_EXPECT(inOwnerDir(*env.current(), bob, tid));
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 2);
        }
        {
            // Test with adding the uritoken to the recipient's owner
            // directory
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, gw);
            env.close();
            env.trust(USD(100000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(1000)));
            env(pay(gw, bob, USD(1000)));
            env.close();

            env(mint(alice, uri));
            env(sell(alice, hexid, USD(10)));
            env.close();
            BEAST_EXPECT(inOwnerDir(*env.current(), alice, tid));
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 2);
            BEAST_EXPECT(!inOwnerDir(*env.current(), bob, tid));
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 1);
            // // alice sets the sell offer
            // // bob sets the buy offer
            env(buy(bob, hexid, USD(10)));
            env.close();
            BEAST_EXPECT(!inOwnerDir(*env.current(), alice, tid));
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 1);
            BEAST_EXPECT(inOwnerDir(*env.current(), bob, tid));
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 2);
        }
    }

    // TODO: THIS TEST IS NOT COMPLETE
    void
    testAccountDelete(FeatureBitset features)
    {
        testcase("account_delete");
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
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        std::string const uri(maxTokenURILength, '?');
        auto const tid = tokenid(alice, uri);
        std::string const hexid{strHex(tid)};

        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol, gw);
            env.close();
            env.trust(USD(100000), alice, bob, carol);
            env.close();
            env(pay(gw, alice, USD(1000)));
            env(pay(gw, bob, USD(1000)));
            env(pay(gw, carol, USD(1000)));
            env.close();

            // mint a uritoken from alice
            env(mint(alice, uri));
            env.close();
            BEAST_EXPECT(inOwnerDir(*env.current(), alice, tid));
            env(sell(alice, hexid, USD(10)));
            env.close();

            // alice has trustline + mint + sell
            rmAccount(env, alice, bob, tecHAS_OBLIGATIONS);

            env(clear(alice, hexid));
            env(burn(alice, hexid));
            env.close();
            BEAST_EXPECT(!inOwnerDir(*env.current(), alice, tid));

            // alice still has a trustline
            rmAccount(env, alice, bob, tecHAS_OBLIGATIONS);
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 1);

            // drain pay all back and drain trustlin
            env.trust(USD(0), alice);
            env(pay(alice, gw, env.balance(alice, USD.issue())));
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 0);

            // alice can delete account
            rmAccount(env, alice, bob);

            // buy should fail if the uri token was removed
            auto preBob = env.balance(bob, USD.issue());
            env(buy(bob, hexid, USD(10)), ter(tecNO_ENTRY));
            env.close();
            BEAST_EXPECT(env.balance(bob, USD.issue()) == preBob);

            // bob can mint same exact token because alice burned it
            env(mint(bob, uri));
            // need to use bobs account for tokenid
            auto const btid = tokenid(bob, uri);
            BEAST_EXPECT(inOwnerDir(*env.current(), bob, btid));
        }
    }

    void
    testTickets(FeatureBitset features)
    {
        testcase("tickets");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto USD = gw["USD"];
        env.fund(XRP(1000), alice, bob, gw);
        env.close();
        env.trust(USD(100000), alice, bob);
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
        auto const tid = tokenid(alice, uri);
        std::string const hexid{strHex(tid)};

        env(mint(alice, uri), ticket::use(aliceTicketSeq++));
        env(sell(alice, hexid, USD(1000)), ticket::use(aliceTicketSeq++));

        env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        BEAST_EXPECT(env.seq(alice) == aliceSeq);
        BEAST_EXPECT(inOwnerDir(*env.current(), alice, tid));

        // A transaction that generates a tec still consumes its ticket.
        env(buy(bob, hexid, USD(1500)),
            ticket::use(bobTicketSeq++),
            ter(tecINSUFFICIENT_FUNDS));
        env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));

        env(buy(bob, hexid, USD(1000)), ticket::use(bobTicketSeq++));

        env.require(tickets(bob, env.seq(bob) - bobTicketSeq));
        BEAST_EXPECT(env.seq(bob) == bobSeq);
        BEAST_EXPECT(inOwnerDir(*env.current(), bob, tid));
    }

    void
    testRippleState(FeatureBitset features)
    {
        testcase("ripple_state");
        using namespace test::jtx;
        using namespace std::literals;

        //
        // USE lineBalance(env, ...) over env.balance(...)
        // I did this to check the exact sign "-/+"
        //

        struct TestAccountData
        {
            Account src;
            Account dst;
            Account gw;
            bool hasTrustline;
            bool negative;
        };

        std::array<TestAccountData, 8> tests = {{
            // src > dst && src > issuer && dst no trustline
            {Account("alice2"), Account("bob0"), Account{"gw0"}, false, true},
            // src < dst && src < issuer && dst no trustline
            {Account("carol0"), Account("dan1"), Account{"gw1"}, false, false},
            // dst > src && dst > issuer && dst no trustline
            {Account("dan1"), Account("alice2"), Account{"gw0"}, false, true},
            // dst < src && dst < issuer && dst no trustline
            {Account("bob0"), Account("carol0"), Account{"gw1"}, false, false},
            // src > dst && src > issuer && dst has trustline
            {Account("alice2"), Account("bob0"), Account{"gw0"}, true, true},
            // src < dst && src < issuer && dst has trustline
            {Account("carol0"), Account("dan1"), Account{"gw1"}, true, false},
            // dst > src && dst > issuer && dst has trustline
            {Account("dan1"), Account("alice2"), Account{"gw0"}, true, true},
            // dst < src && dst < issuer && dst has trustline
            {Account("bob0"), Account("carol0"), Account{"gw1"}, true, false},
        }};

        for (auto const& t : tests)
        {
            Env env{*this, features};
            auto const USD = t.gw["USD"];
            env.fund(XRP(5000), t.src, t.dst, t.gw);
            env.close();
            if (t.hasTrustline)
                env.trust(USD(100000), t.src, t.dst);
            else
                env.trust(USD(100000), t.src);
            env.close();

            env(pay(t.gw, t.src, USD(10000)));
            if (t.hasTrustline)
                env(pay(t.gw, t.dst, USD(10000)));
            env.close();

            // dst can create uritoken
            std::string const uri(maxTokenURILength, '?');
            std::string const id{strHex(tokenid(t.dst, uri))};
            env(mint(t.dst, uri));
            env.close();

            // dst can create sell
            auto const delta = USD(1000);
            auto const preSrc = lineBalance(env, t.src, t.gw, USD);
            auto const preDst = lineBalance(env, t.dst, t.gw, USD);
            env(sell(t.dst, id, delta));
            env.close();
            BEAST_EXPECT(preDst == preDst);

            // src can create buy
            env(buy(t.src, id, delta));
            env.close();
            BEAST_EXPECT(
                lineBalance(env, t.src, t.gw, USD) ==
                (t.negative ? (preSrc + delta) : (preSrc - delta)));
            BEAST_EXPECT(
                lineBalance(env, t.dst, t.gw, USD) ==
                (t.negative ? (preDst - delta) : (preDst + delta)));
        }
    }

    void
    testGateway(FeatureBitset features)
    {
        testcase("gateway");
        using namespace test::jtx;
        using namespace std::literals;

        struct TestAccountData
        {
            Account acct;
            Account gw;
            bool hasTrustline;
            bool negative;
        };

        std::array<TestAccountData, 4> tests = {{
            // acct no trustline
            // acct > issuer
            {Account("alice2"), Account{"gw0"}, false, true},
            // acct < issuer
            {Account("carol0"), Account{"gw1"}, false, false},

            // acct has trustline
            // acct > issuer
            {Account("alice2"), Account{"gw0"}, true, true},
            // acct < issuer
            {Account("carol0"), Account{"gw1"}, true, false},
        }};

        // test gateway is buyer
        for (auto const& t : tests)
        {
            Env env{*this, features};
            auto const USD = t.gw["USD"];
            env.fund(XRP(5000), t.acct, t.gw);
            env.close();

            if (t.hasTrustline)
                env.trust(USD(100000), t.acct);

            env.close();

            if (t.hasTrustline)
                env(pay(t.gw, t.acct, USD(10000)));
            env.close();

            // acct can create uritoken
            std::string const uri(maxTokenURILength, '?');
            std::string const id{strHex(tokenid(t.acct, uri))};
            env(mint(t.acct, uri));
            env.close();

            // acct can create sell w/out token
            auto const delta = USD(1000);
            auto const preAcct = lineBalance(env, t.acct, t.gw, USD);
            auto const preGw = lineBalance(env, t.gw, t.acct, USD);
            env(sell(t.acct, id, delta));
            env.close();
            auto const preAmount = t.hasTrustline ? 10000 : 0;
            BEAST_EXPECT(
                preAcct == (t.negative ? -USD(preAmount) : USD(preAmount)));

            // gw can create buy
            env(buy(t.gw, id, delta));
            env.close();
            auto const postAmount = t.hasTrustline ? 11000 : 1000;
            BEAST_EXPECT(
                lineBalance(env, t.acct, t.gw, USD) ==
                (t.negative ? -USD(postAmount) : USD(postAmount)));
            BEAST_EXPECT(
                lineBalance(env, t.gw, t.acct, USD) ==
                (t.negative ? -USD(postAmount) : USD(postAmount)));
        }

        // test gateway is seller
        // ignore hasTrustline
        for (auto const& t : tests)
        {
            Env env{*this, features};
            auto const USD = t.gw["USD"];
            env.fund(XRP(5000), t.acct, t.gw);
            env.close();
            env.trust(USD(100000), t.acct);
            env.close();
            env(pay(t.gw, t.acct, USD(10000)));
            env.close();

            // gw can create uritoken
            std::string const uri(maxTokenURILength, '?');
            std::string const id{strHex(tokenid(t.gw, uri))};
            env(mint(t.gw, uri));
            env.close();

            // gw can create sell w/out token
            auto const delta = USD(1000);
            auto const preAcct = lineBalance(env, t.acct, t.gw, USD);
            auto const preGw = lineBalance(env, t.gw, t.acct, USD);
            env(sell(t.gw, id, delta));
            env.close();
            auto const preAmount = 10000;
            BEAST_EXPECT(
                preAcct == (t.negative ? -USD(preAmount) : USD(preAmount)));

            // acct can create buy
            env(buy(t.acct, id, delta));
            env.close();
            auto const postAmount = 9000;
            BEAST_EXPECT(
                lineBalance(env, t.acct, t.gw, USD) ==
                (t.negative ? -USD(postAmount) : USD(postAmount)));
            BEAST_EXPECT(
                lineBalance(env, t.gw, t.acct, USD) ==
                (t.negative ? -USD(postAmount) : USD(postAmount)));
        }
    }

    void
    testRequireAuth(FeatureBitset features)
    {
        testcase("require_auth");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        auto const aliceUSD = alice["USD"];
        auto const bobUSD = bob["USD"];

        // test asfRequireAuth
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, gw);
            env(fset(gw, asfRequireAuth));
            env.close();
            env(trust(gw, bobUSD(10000)), txflags(tfSetfAuth));
            env(trust(bob, USD(10000)));
            env.close();
            env(pay(gw, bob, USD(1000)));
            env.close();

            std::string const uri(maxTokenURILength, '?');
            auto const tid = tokenid(alice, uri);
            std::string const hexid{strHex(tid)};
            env(mint(alice, uri));
            env(sell(alice, hexid, USD(10)));
            env.close();

            // bob cannot buy because alice's trustline is not authorized
            // all parties must be authorized
            env(buy(bob, hexid, USD(10)), ter(tecNO_AUTH));
            env.close();

            env(trust(gw, aliceUSD(10000)), txflags(tfSetfAuth));
            env(trust(alice, USD(10000)));
            env.close();
            env(pay(gw, alice, USD(1000)));
            env.close();

            // bob can now buy because alice's trustline is authorized
            env(buy(bob, hexid, USD(10)));
            env.close();
        }
    }

    void
    testFreeze(FeatureBitset features)
    {
        testcase("freeze");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        // test Global Freeze
        {
            // setup env
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, gw);
            env.close();
            env.trust(USD(100000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(1000)));
            env(pay(gw, bob, USD(1000)));
            env.close();
            env(fset(gw, asfGlobalFreeze));
            env.close();

            // setup mint
            std::string const uri(maxTokenURILength, '?');
            auto const tid = tokenid(alice, uri);
            std::string const hexid{strHex(tid)};
            env(mint(alice, uri));
            env(sell(alice, hexid, USD(10)));
            env.close();

            // bob cannot buy
            env(buy(bob, hexid, USD(10)), ter(tecINSUFFICIENT_FUNDS));
            env.close();

            // clear global freeze
            env(fclear(gw, asfGlobalFreeze));
            env.close();

            // bob can buy
            env(buy(bob, hexid, USD(10)));
            env.close();
        }
        // test Individual Freeze
        {
            // Env Setup
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, gw);
            env.close();
            env.trust(USD(100000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(1000)));
            env(pay(gw, bob, USD(1000)));
            env.close();

            // set freeze on alice trustline
            env(trust(gw, USD(10000), bob, tfSetFreeze));
            env.close();

            // setup mint
            std::string const uri(maxTokenURILength, '?');
            auto const tid = tokenid(alice, uri);
            std::string const hexid{strHex(tid)};
            env(mint(alice, uri));
            env(sell(alice, hexid, USD(10)));
            env.close();

            // buy uritoken fails - frozen trustline
            env(buy(bob, hexid, USD(10)), ter(tecINSUFFICIENT_FUNDS));
            env.close();

            // clear freeze on alice trustline
            env(trust(gw, USD(10000), bob, tfClearFreeze));
            env.close();

            // buy uri success
            env(buy(bob, hexid, USD(10)));
            env.close();
        }
    }

    void
    testTransferRate(FeatureBitset features)
    {
        testcase("transfer_rate");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        // test transfer rate
        {
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env(rate(gw, 1.25));
            env.close();
            env.trust(USD(100000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(1000)));
            env(pay(gw, bob, USD(1000)));
            env.close();

            auto const preBob = env.balance(bob, USD.issue());

            // setup mint
            std::string const uri(maxTokenURILength, '?');
            std::string const id{strHex(tokenid(alice, uri))};
            auto const delta = USD(100);
            env(mint(alice, uri));
            env(sell(alice, id, delta));
            env.close();

            env(buy(bob, id, delta));
            env.close();
            BEAST_EXPECT(env.balance(alice, USD.issue()) == USD(1125));
            BEAST_EXPECT(env.balance(bob, USD.issue()) == preBob - delta);
        }
        // test rate change
        {
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env(rate(gw, 1.25));
            env.close();
            env.trust(USD(100000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();

            // setup
            std::string const uri(maxTokenURILength, '?');
            std::string const id{strHex(tokenid(alice, uri))};
            auto const delta = USD(100);
            auto preBob = env.balance(bob, USD.issue());
            // alice mints and sells
            env(mint(alice, uri));
            env(sell(alice, id, delta));
            env.close();

            // bob buys at higher rate and burns
            env(buy(bob, id, delta));
            env.close();
            BEAST_EXPECT(env.balance(alice, USD.issue()) == USD(10125));
            BEAST_EXPECT(env.balance(bob, USD.issue()) == preBob - delta);
            env(burn(bob, id));

            // issuer changes rate lower
            env(rate(gw, 1.00));
            env.close();

            preBob = env.balance(bob, USD.issue());

            // alice mints and sells
            env(mint(alice, uri));
            env(sell(alice, id, delta));
            env.close();

            // bob buys at lower rate
            env(buy(bob, id, delta));
            env.close();
            BEAST_EXPECT(env.balance(alice, USD.issue()) == USD(10225));
            BEAST_EXPECT(env.balance(bob, USD.issue()) == preBob - delta);
        }
        // test issuer doesnt pay own rate
        {
            Env env{*this, features};
            env.fund(XRP(10000), alice, gw);
            env(rate(gw, 1.25));
            env.close();
            env.trust(USD(100000), alice);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env.close();

            std::string const uri(maxTokenURILength, '?');
            auto const tid = tokenid(alice, uri);
            std::string const hexid{strHex(tid)};

            auto const delta = USD(10);
            auto const preAlice = env.balance(alice, USD.issue());

            // alice mints
            env(mint(alice, uri));
            env.close();
            // alice sells
            env(sell(alice, hexid, delta));
            env.close();

            // gw buys
            env(buy(gw, hexid, delta));
            env.close();
            BEAST_EXPECT(env.balance(alice, USD.issue()) == preAlice + delta);
        }
    }

    void
    testDisallowXRP(FeatureBitset features)
    {
        // auth amount defaults to balance if not present
        testcase("disallow_xrp");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        {
            // Create a channel where src/dst disallows XRP
            // Ignore that flag, since it's just advisory.
            Env env(*this, features);
            env.fund(XRP(10000), alice, bob);
            env(fset(alice, asfDisallowXRP));
            env(fset(bob, asfDisallowXRP));
            env.close();

            std::string const uri(maxTokenURILength, '?');
            auto const tid = tokenid(alice, uri);
            std::string const hexid{strHex(tid)};

            // alice mints
            env(mint(alice, uri));
            env.close();

            // alice sells
            env(sell(alice, hexid, XRP(10)));
            env.close();

            // bob buys
            env(buy(bob, hexid, XRP(10)));
            env.close();
        }
    }

    void
    testLimitAmount(FeatureBitset features)
    {
        testcase("limit_amount");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        {
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, carol, gw);
            env.close();
            env.trust(USD(1000), bob);
            env.trust(USD(1000), carol);
            env.close();
            env(pay(gw, bob, USD(1000)));
            env(pay(gw, carol, USD(1000)));
            env.close();
            std::string const uri(maxTokenURILength, '?');
            auto const tid = tokenid(alice, uri);
            std::string const hexid{strHex(tid)};
            env(mint(alice, uri));
            env.close();
            BEAST_EXPECT(inOwnerDir(*env.current(), alice, tid));
            env(sell(alice, hexid, USD(10)));
            env.close();
            auto preLimit = limitAmount(env, alice, gw, USD);
            BEAST_EXPECT(preLimit == USD(0));
            env(buy(bob, hexid, USD(10)));
            env.close();
            auto const postLimit = limitAmount(env, bob, gw, USD);
            BEAST_EXPECT(postLimit == preLimit);
            env(pay(alice, carol, USD(1)), ter(tecPATH_DRY));
        }
    }

    void
    testURIUTF8(FeatureBitset features)
    {
        // https://www.cl.cam.ac.uk/~mgk25/ucs/examples/UTF-8-test.txt
        testcase("uri_utf8");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env{*this, features};
        env.fund(XRP(10000), alice, bob);
        env.close();

        std::string uri = "";

        // test utf-8 success
        {
            // case: kosme
            uri = "κόσμε";
            env(mint(alice, uri));

            // case: single ASCII character
            uri = "a";
            env(mint(alice, uri));

            // case: single non-ASCII character
            uri = "é";
            env(mint(alice, uri));

            // case: valid multi-byte UTF-8 sequence
            uri = "€";
            env(mint(alice, uri));

            // case: ipfs cid
            uri = "QmaCtDKZFVvvfufvbdy4estZbhQH7DXh16CTpv1howmBGy";
            env(mint(alice, uri));

            // case: empty ipfs cid url
            uri = "ipfs://";
            env(mint(alice, uri));

            // case: ipfs cid url
            uri = "ipfs://QmaCtDKZFVvvfufvbdy4estZbhQH7DXh16CTpv1howmBGy";
            env(mint(alice, uri));

            // case: ipfs metadata url
            uri = "https://example.com/ipfs/";
            env(mint(alice, uri));

            // BOUNDRY - START
            // ----------------------------------------------------------------

            // case: 1 byte  (U-00000000)
            uri = "\x00";
            env(mint(alice, uri),
                ter(temMALFORMED));  // TODO: REVIEW - SHOULD NOT FAIL
            // case: 2 bytes (U-00000080)
            uri = "\xC2\x80";
            env(mint(alice, uri));
            // case: 3 bytes (U-00000800)
            uri = "\xE0\xA0\x80";
            env(mint(alice, uri));
            // case: 4 bytes (U-00010000)
            uri = "\xF0\x90\x80\x80";
            env(mint(alice, uri));
            // case: 5 bytes (U-00200000)
            uri = "\xF8\x88\x80\x80\x80";
            env(mint(alice, uri),
                ter(temMALFORMED));  // TODO: REVIEW - SHOULD NOT FAIL
            // case: 6 bytes (U-04000000)
            uri = "\xFC\x84\x80\x80\x80\x80";
            env(mint(alice, uri),
                ter(temMALFORMED));  // TODO: REVIEW - SHOULD NOT FAIL

            // BOUNDRY - END
            // ----------------------------------------------------------------

            // case: 1 byte  (U-0000007F)
            uri = "\x7F";
            env(mint(alice, uri));
            // case: 2 bytes (U-000007FF)
            uri = "\xDF\xBF";
            env(mint(alice, uri));
            // case: 3 bytes (U-0000FFFF)
            uri = "\xEF\xBF\xBF";
            env(mint(alice, uri),
                ter(temMALFORMED));  // TODO: REVIEW - SHOULD NOT FAIL
            // case: 4 bytes (U-001FFFFF)
            uri = "\xF7\xBF\xBF\xBF";
            env(mint(alice, uri),
                ter(temMALFORMED));  // TODO: REVIEW - SHOULD NOT FAIL
            // case: 5 bytes (U-03FFFFFF)
            uri = "\xFB\xBF\xBF\xBF\xBF";
            env(mint(alice, uri),
                ter(temMALFORMED));  // TODO: REVIEW - SHOULD NOT FAIL
            // case: 6 bytes (U-7FFFFFFF)
            uri = "\xFD\xBF\xBF\xBF\xBF\xBF";
            env(mint(alice, uri),
                ter(temMALFORMED));  // TODO: REVIEW - SHOULD NOT FAIL

            // // BOUNDRY - OTHER
            // ----------------------------------------------------------------
            // case: 1 bytes (U-0000D7FF)
            uri = "\xD7\xFF";
            env(mint(alice, uri),
                ter(temMALFORMED));  // TODO: REVIEW - SHOULD NOT FAIL
            // case: 2 bytes (U-0000E000)
            uri = "\xEE\x80\x80";
            env(mint(alice, uri));
            // case: 3 bytes (U-0000FFFD)
            uri = "\xEF\xBF\xBD";
            env(mint(alice, uri));
            // // case: 4 bytes (U-0010FFFF)
            uri = "\xF4\x8F\xBF\xBF";
            env(mint(alice, uri));
            // // case: 4 bytes (U-00110000)
            uri = "\xF4\x90\x80\x80";
            env(mint(alice, uri),
                ter(temMALFORMED));  // TODO: REVIEW - SHOULD NOT FAIL
        }
        // test utf8 malformed
        {
            // MALFORMED - END
            // ----------------------------------------------------------------
            // First continuation byte 0x80:
            uri = "\x80";
            env(mint(alice, uri), ter(temMALFORMED));
            // Last continuation byte 0xbf
            uri = "\xBF";
            env(mint(alice, uri), ter(temMALFORMED));

            // 2 continuation bytes
            uri = "��";
            env(mint(alice, uri));  // TODO: REVIEW - SHOULD FAIL

            // 3 continuation bytes
            uri = "���";
            env(mint(alice, uri));  // TODO: REVIEW - SHOULD FAIL

            // 4 continuation bytes
            uri = "����";
            env(mint(alice, uri));  // TODO: REVIEW - SHOULD FAIL

            // 5 continuation bytes
            uri = "�����";
            env(mint(alice, uri));  // TODO: REVIEW - SHOULD FAIL

            // 6 continuation bytes
            uri = "������";
            env(mint(alice, uri));  // TODO: REVIEW - SHOULD FAIL

            // 7 continuation bytes
            uri = "�������";
            env(mint(alice, uri));  // TODO: REVIEW - SHOULD FAIL

            // Sequence of all 64 possible continuation bytes (0x80-0xbf)
            uri =
                "\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8A\x8B\x8C\x8D\x8E"
                "\x8F\x90\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9A\x9B\x9C\x9D"
                "\x9E\x9F\xA0\xA1\xA2\xA3\xA4\xA5\xA6\xA7\xA8\xA9\xAA\xAB\xAC"
                "\xAD\xAE\xAF\xB0\xB1\xB2\xB3\xB4\xB5\xB6\xB7\xB8\xB9\xBA\xBB"
                "\xBC\xBD\xBE\xBF";
            env(mint(alice, uri), ter(temMALFORMED));

            // TODO: REVIEW - THIS IS NOT THE CORRECT 32 byte sequence.
            // All 32 first bytes of 2-byte sequences (0xc0-0xdf), each followed
            // by a space character
            // uri = "\xE0\x80\x80 \xE0\x80\x81 \xE0\x80\x82 \xE0\x80\x83
            // \xE0\x80\x84 \xE0\x80\x85 \xE0\x80\x86 \xE0\x80\x87 \xE0\x80\x88
            // \xE0\x80\x89 \xE0\x80\x8A \xE0\x80\x8B \xE0\x80\x8C \xE0\x80\x8D
            // \xE0\x80\x8E \xE0\x80\x8F \xE0\x80\x90"; env(mint(alice, uri),
            // ter(temMALFORMED));

            // All 16 first bytes of 3-byte sequences (0xe0-0xef), each followed
            // by a space character
            uri =
                "\xE0\x80\x80 \xE0\x80\x81 \xE0\x80\x82 \xE0\x80\x83 "
                "\xE0\x80\x84 \xE0\x80\x85 \xE0\x80\x86 \xE0\x80\x87 "
                "\xE0\x80\x88 \xE0\x80\x89 \xE0\x80\x8A \xE0\x80\x8B "
                "\xE0\x80\x8C \xE0\x80\x8D \xE0\x80\x8E \xE0\x80\x8F "
                "\xE0\x80\x90";
            env(mint(alice, uri), ter(temMALFORMED));

            // All 8 first bytes of 4-byte sequences (0xf0-0xf7), each followed
            // by a space character
            uri =
                "\xF0\x90\x80\x80 \xF0\x90\x80\x81 \xF0\x90\x80\x82 "
                "\xF0\x90\x80\x83 \xF0\x90\x80\x84 \xF0\x90\x80\x85 "
                "\xF0\x90\x80\x86 \xF0\x90\x80\x87";
            env(mint(alice, uri));  // TODO: REVIEW - SHOULD FAIL

            // All 4 first bytes of 5-byte sequences (0xf8-0xfb), each followed
            // by a space character
            uri =
                "\xF8\x88\x80\x80\x80 \xF8\x88\x80\x80\x81 "
                "\xF8\x88\x80\x80\x82 \xF8\x88\x80\x80\x83";
            env(mint(alice, uri),
                ter(temMALFORMED));  // TODO: REVIEW - SHOULD FAIL

            // All 2 first bytes of 6-byte sequences (0xfc-0xfd), each followed
            // by a space character
            uri = "\xFC\x84\x80\x80\x80\x80 \xFC\x84\x80\x80\x80\x81";
            env(mint(alice, uri), ter(temMALFORMED));

            // Sequences with last continuation byte missing

            // Concatenation of incomplete sequences

            // Impossible bytes
            uri = "\xFE";
            env(mint(alice, uri), ter(temMALFORMED));
            uri = "\xFF";
            env(mint(alice, uri), ter(temMALFORMED));
            uri = "\xFE\xFE\xFF\xFF";
            env(mint(alice, uri), ter(temMALFORMED));

            // Examples of an overlong ASCII character
            // case: (U+002F)
            uri = "\xC0\xAF";
            env(mint(alice, uri), ter(temMALFORMED));
            // case: (U+002F)
            uri = "\xE0\x80\xAF";
            env(mint(alice, uri), ter(temMALFORMED));
            // case: (U+002F)
            uri = "\xF0\x80\x80\xAF";
            env(mint(alice, uri), ter(temMALFORMED));
            // case: (U+002F)
            uri = "\xF0\x80\x80\x80\xAF";
            env(mint(alice, uri), ter(temMALFORMED));
            // case: (U+002F)
            uri = "\xF0\x80\x80\x80\x80\xAF";
            env(mint(alice, uri), ter(temMALFORMED));

            // Maximum overlong sequences
            // case: (U+0000007F)
            uri = "\xC1\xBF";
            env(mint(alice, uri), ter(temMALFORMED));
            // case: (U+000007FF)
            uri = "\xE0\x9F\xBF";
            env(mint(alice, uri), ter(temMALFORMED));
            // case: (U+0000FFFF)
            uri = "\xF0\x8F\xBF\xBF";
            env(mint(alice, uri), ter(temMALFORMED));
            // case: (U+001FFFFF)
            uri = "\xF8\x87\xBF\xBF\xBF";
            env(mint(alice, uri), ter(temMALFORMED));
            // case: (U+03FFFFFF)
            uri = "\xFC\x83\xBF\xBF\xBF\xBF";
            env(mint(alice, uri), ter(temMALFORMED));

            // Overlong representation of the NUL character
            // case: (U+0000)
            uri = "\xC0\x80";
            env(mint(alice, uri), ter(temMALFORMED));
            // case: (U+0000)
            uri = "\xC0\x80\x80";
            env(mint(alice, uri), ter(temMALFORMED));
            // case: (U+0000)
            uri = "\xC0\x80\x80\x80";
            env(mint(alice, uri), ter(temMALFORMED));
            // case: (U+0000)
            uri = "\xC0\x80\x80\x80\x80";
            env(mint(alice, uri), ter(temMALFORMED));
            // case: (U+0000)
            uri = "\xC0\x80\x80\x80\x80\x80";
            env(mint(alice, uri), ter(temMALFORMED));

            // Single UTF-16 surrogates
            // case: (U+D800)
            uri = "\xED\xA0\x80";
            env(mint(alice, uri), ter(temMALFORMED));
            // case: (U+DB7F)
            uri = "\xED\xAD\xBF";
            env(mint(alice, uri), ter(temMALFORMED));
            // case: (U+DB80)
            uri = "\xED\xAE\x80";
            env(mint(alice, uri), ter(temMALFORMED));
            // case: (U+DBFF)
            uri = "\xED\xAF\xBF";
            env(mint(alice, uri), ter(temMALFORMED));
            // case: (U+DC00)
            uri = "\xED\xB0\x80";
            env(mint(alice, uri), ter(temMALFORMED));
            // case: (U+DF80)
            uri = "\xED\xBE\x80";
            env(mint(alice, uri), ter(temMALFORMED));
            // case: (U+DFFF)
            uri = "\xED\xBF\xBF";
            env(mint(alice, uri), ter(temMALFORMED));

            // Paired UTF-16 surrogates
            // case: (U+D800 U+DC00)
            uri = "\xED\xA0\x80\xED\xB0\x80";
            env(mint(alice, uri), ter(temMALFORMED));
            // case: (U+D800 U+DFFF)
            uri = "\xED\xA0\x80\xED\xBF\xBF";
            env(mint(alice, uri), ter(temMALFORMED));
            // case: (U+DB7F U+DC00)
            uri = "\xED\xAD\xBF\xED\xB0\x80";
            env(mint(alice, uri), ter(temMALFORMED));
            // case: (U+DB7F U+DFFF)
            uri = "\xED\xAD\xBF\xED\xBF\xBF";
            env(mint(alice, uri), ter(temMALFORMED));
            // case: (U+DB80 U+DC00)
            uri = "\xED\xAE\x80\xED\xB0\x80";
            env(mint(alice, uri), ter(temMALFORMED));
            // case: (U+DB80 U+DFFF)
            uri = "\xED\xAE\x80\xED\xBF\xBF";
            env(mint(alice, uri), ter(temMALFORMED));
            // case: (U+DBFF U+DC00)
            uri = "\xED\xAF\xBF\xED\xB0\x80";
            env(mint(alice, uri), ter(temMALFORMED));
            // case: (U+DBFF U+DFFF)
            uri = "\xED\xAF\xBF\xED\xBF\xBF";
            env(mint(alice, uri), ter(temMALFORMED));

            // problematic noncharacters in 16-bit applications
            // case: (U+FFFE)
            uri = "\xEF\xBF\xBE";
            env(mint(alice, uri), ter(temMALFORMED));
            // case: (U+FFFF)
            uri = "\xEF\xBF\xBF";
            env(mint(alice, uri), ter(temMALFORMED));
        }
    }
    void
    testWithFeats(FeatureBitset features)
    {
        testEnabled(features);
        testMintInvalid(features);
        testBurnInvalid(features);
        testSellInvalid(features);
        testBuyInvalid(features);
        testClearInvalid(features);
        testMintValid(features);
        testBurnValid(features);
        testBuyValid(features);
        testSellValid(features);
        testClearValid(features);
        testMetaAndOwnership(features);
        testAccountDelete(features);
        testTickets(features);
        testRippleState(features);
        testGateway(features);
        testRequireAuth(features);
        testFreeze(features);
        testTransferRate(features);
        testDisallowXRP(features);
        testLimitAmount(features);
        testURIUTF8(features);
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
