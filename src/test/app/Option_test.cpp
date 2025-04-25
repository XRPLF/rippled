//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#include <test/jtx.h>

#include <xrpld/ledger/Dir.h>

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {

struct Option_test : public beast::unit_test::suite
{
    static bool
    inOwnerDir(
        ReadView const& view,
        jtx::Account const& acct,
        uint256 const& tid)
    {
        auto const sle = view.read({ltOPTION_OFFER, tid});
        ripple::Dir const ownerDir(view, keylet::ownerDir(acct.id()));
        return std::find(ownerDir.begin(), ownerDir.end(), sle) !=
            ownerDir.end();
    }

    static std::size_t
    ownerDirCount(ReadView const& view, jtx::Account const& acct)
    {
        ripple::Dir const ownerDir(view, keylet::ownerDir(acct.id()));
        return std::distance(ownerDir.begin(), ownerDir.end());
    };

    static bool
    validateOption(
        ReadView const& view,
        uint256 const& optionId,
        NetClock::time_point expiration,
        STAmount const& strikePrice,
        STIssue const& asset)
    {
        auto const k = keylet::unchecked(optionId);
        auto const sle = view.read(k);
        if (!sle)
            return false;
        if ((*sle)[sfExpiration] != expiration.time_since_epoch().count())
            return false;
        if ((*sle)[sfStrikePrice] != strikePrice)
            return false;
        if ((*sle)[sfAsset] != asset)
            return false;
        return true;
    }

    struct SealedOption
    {
        uint256 offerId;
        AccountID owner;
        std::uint32_t quantity;
    };

    void
    validateOffer(
        int line,
        ReadView const& view,
        uint256 const& offerId,
        std::uint32_t const& quantity,
        STAmount const& premium,
        STAmount const& lockedAmount,
        std::uint32_t const& openInterest,
        std::vector<SealedOption> const& sealedOptions_)
    {
        using namespace std::string_literals;

        auto const k = keylet::unchecked(offerId);
        auto const sle = view.read(k);
        if (!sle)
            fail("Option offer not found in ledger"s, __FILE__, line);
        return;

        if ((*sle)[sfQuantity] != quantity)
            fail(
                "Quantity mismatch: "s + std::to_string((*sle)[sfQuantity]) +
                    "/" + std::to_string(quantity),
                __FILE__,
                line);

        if ((*sle)[sfPremium] != premium)
            fail(
                "Premium mismatch: "s + (*sle)[sfPremium].getFullText() + "/" +
                    premium.getFullText(),
                __FILE__,
                line);
        if (lockedAmount && !sle->isFieldPresent(sfAmount) &&
            (*sle)[sfAmount] != lockedAmount)
            fail(
                "Locked amount field not present, but expected: "s +
                    lockedAmount.getFullText(),
                __FILE__,
                line);
        else if (
            !lockedAmount && sle->isFieldPresent(sfAmount) &&
            (*sle)[sfAmount] != STAmount(0))
            fail(
                "Locked amount field present, but expected to be absent",
                __FILE__,
                line);
        else if ((*sle)[sfAmount] && (*sle)[sfAmount] != lockedAmount)
            fail(
                "Locked amount mismatch: "s + (*sle)[sfAmount].getFullText() +
                    "/" + lockedAmount.getFullText(),
                __FILE__,
                line);
        if (openInterest && !(*sle)[sfOpenInterest])
            fail(
                "Open interest field not present, but expected: "s +
                    std::to_string(openInterest),
                __FILE__,
                line);
        else if (!openInterest && (*sle)[sfOpenInterest])
            fail(
                "Open interest field present, but expected to be absent",
                __FILE__,
                line);
        else if (
            (*sle)[sfOpenInterest] && (*sle)[sfOpenInterest] != openInterest)
            fail(
                "Open interest mismatch: "s +
                    std::to_string(
                        static_cast<std::uint32_t>((*sle)[sfOpenInterest])) +
                    "/" + std::to_string(openInterest),
                __FILE__,
                line);
        if (sealedOptions_.size() > 0 && !sle->isFieldPresent(sfSealedOptions))
            fail(
                "Expected sealed options field to be present with "s +
                    std::to_string(sealedOptions_.size()) +
                    " entries, but field is missing",
                __FILE__,
                line);
        else if (
            sealedOptions_.size() == 0 &&
            sle->isFieldPresent(sfSealedOptions) &&
            sle->getFieldArray(sfSealedOptions).size() > 0)
            fail(
                "Expected sealed options field to be absent, but field is "
                "present",
                __FILE__,
                line);
        else if (
            sealedOptions_.size() > 0 && sle->isFieldPresent(sfSealedOptions))
        {
            STArray const sealedOptions = sle->getFieldArray(sfSealedOptions);
            if (sealedOptions.size() != sealedOptions_.size())
                fail(
                    "Sealed options count mismatch: "s +
                        std::to_string(sealedOptions.size()) + "/" +
                        std::to_string(sealedOptions_.size()),
                    __FILE__,
                    line);

            for (std::size_t i = 0; i < sealedOptions.size(); ++i)
            {
                auto const sealedOption = sealedOptions[i];
                auto const slOfferId =
                    sealedOption.getFieldH256(sfOptionOfferID);
                auto const slOwner = sealedOption.getAccountID(sfOwner);
                auto const slQuantity = sealedOption.getFieldU32(sfQuantity);

                if (slOfferId != sealedOptions_[i].offerId)
                    fail(
                        "Sealed option #"s + std::to_string(i) +
                            " offer ID mismatch: " + to_string(slOfferId) +
                            "/" + to_string(sealedOptions_[i].offerId),
                        __FILE__,
                        line);

                if (slOwner != sealedOptions_[i].owner)
                    fail(
                        "Sealed option #"s + std::to_string(i) +
                            " owner mismatch: " + to_string(slOwner) + "/" +
                            to_string(sealedOptions_[i].owner),
                        __FILE__,
                        line);

                if (slQuantity != sealedOptions_[i].quantity)
                    fail(
                        "Sealed option #"s + std::to_string(i) +
                            " quantity mismatch: " +
                            std::to_string(slQuantity) + "/" +
                            std::to_string(sealedOptions_[i].quantity),
                        __FILE__,
                        line);
            }
        }

        pass();
    }

    // testDebug("PRE", env, { alice, bob }, {});
    void
    testDebug(
        std::string const& testNumber,
        jtx::Env const& env,
        std::vector<jtx::Account> const& accounts,
        std::vector<jtx::IOU> const& ious)
    {
        std::cout << "DEBUG: " << testNumber << "\n";
        for (std::size_t a = 0; a < accounts.size(); ++a)
        {
            auto const bal = env.balance(accounts[a]);
            std::cout << "account: " << accounts[a].human() << "BAL: " << bal
                      << "\n";
            for (std::size_t i = 0; i < ious.size(); ++i)
            {
                auto const iouBal = env.balance(accounts[a], ious[i]);
                std::cout << "account: " << accounts[a].human()
                          << "IOU: " << iouBal << "\n";
            }
        }
    }

    static STAmount
    lockedValue(
        jtx::Env const& env,
        jtx::Account const& account,
        std::uint32_t const& seq)
    {
        auto const sle = env.le(keylet::optionOffer(account, seq));
        if (sle->isFieldPresent(sfAmount))
            return (*sle)[sfAmount];
        return STAmount(0);
    }

    Json::Value
    optionPairCreate(
        jtx::Account const& account,
        STIssue const& asset,
        STIssue const& asset2)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::TransactionType] = jss::OptionPairCreate;
        jv[jss::Account] = account.human();
        jv[sfAsset.jsonName] = asset.getJson(JsonOptions::none);
        jv[sfAsset2.jsonName] = asset2.getJson(JsonOptions::none);
        return jv;
    }

    Json::Value
    optionCreate(
        jtx::Account const& account,
        NetClock::time_point expiration,
        STAmount const& strikePrice,
        STIssue const& asset,
        uint32_t const& quantity,
        STAmount const& premium)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::TransactionType] = jss::OptionCreate;
        jv[jss::Account] = account.human();
        jv[sfStrikePrice.jsonName] = strikePrice.getJson(JsonOptions::none);
        jv[sfAsset.jsonName] = asset.getJson(JsonOptions::none);
        jv[sfExpiration.jsonName] = expiration.time_since_epoch().count();
        jv[sfPremium.jsonName] = premium.getJson(JsonOptions::none);
        jv[sfQuantity.jsonName] = quantity;
        return jv;
    }

    Json::Value
    optionSettle(
        jtx::Account const& account,
        uint256 const& optionId,
        uint256 const& offerId)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::TransactionType] = jss::OptionSettle;
        jv[jss::Account] = account.human();
        jv[sfOptionID.jsonName] = to_string(optionId);
        jv[sfOptionOfferID.jsonName] = to_string(offerId);
        ;
        return jv;
    }

    static uint256
    getOptionIndex(
        AccountID const& issuer,
        Currency const& currency,
        std::uint64_t const& strike,
        NetClock::time_point expiration)
    {
        return keylet::option(
                   issuer,
                   currency,
                   strike,
                   expiration.time_since_epoch().count())
            .key;
    }

    static uint256
    getOfferIndex(AccountID const& account, std::uint32_t sequence)
    {
        return keylet::optionOffer(account, sequence).key;
    }

    // static auto
    // getOptionList(jtx::Env& env, AccountID const& issuer)
    // {
    //     Json::Value jvbp;
    //     jvbp[jss::ledger_index] = "current";
    //     jvbp[jss::account] = to_string(issuer);
    //     jvbp[jss::type] = "option";
    //     return env.rpc("json", "account_objects",
    //     to_string(jvbp))[jss::result];
    // }

    static auto
    getOptionBookOffers(
        jtx::Env& env,
        Issue const& issue,
        STAmount const& strikePrice,
        NetClock::time_point expiration)
    {
        Json::Value jvbp;
        jvbp[jss::ledger_index] = "current";
        jvbp[jss::asset][jss::currency] = to_string(issue.currency);
        jvbp[jss::asset][jss::issuer] = to_string(issue.account);
        jvbp[jss::strike_price] = strikePrice.getJson(JsonOptions::none);
        jvbp[jss::expiration] =
            to_string(expiration.time_since_epoch().count());
        return env.rpc(
            "json", "option_book_offers", to_string(jvbp))[jss::result];
    }

    void
    initPair(
        jtx::Env& env,
        jtx::Account const& account,
        Issue const& issue,
        Issue const& issue2)
    {
        using namespace test::jtx;
        env(optionPairCreate(
                account, STIssue(sfAsset, issue), STIssue(sfAsset, issue2)),
            fee(env.current()->fees().increment),
            ter(tesSUCCESS));
        env.close();
    }

    uint256
    createOffer(
        jtx::Env& env,
        jtx::Account const& account,
        std::uint32_t const& seq,
        jtx::IOU const& AST,
        std::uint32_t const& quantity,
        NetClock::time_point const& expiration,
        STAmount const& strikePrice,
        STAmount const& premium,
        std::uint32_t flags)
    {
        using namespace test::jtx;
        auto const issue = STIssue(sfAsset, AST.issue());
        auto const offerId = getOfferIndex(account.id(), seq);
        env(optionCreate(
                account, expiration, strikePrice, issue, quantity, premium),
            txflags(flags),
            ter(tesSUCCESS));
        env.close();
        return offerId;
    }

    void
    testEnabled(FeatureBitset features)
    {
        using namespace test::jtx;
        using namespace std::literals::chrono_literals;
        testcase("enabled");

        for (bool const withOptions : {true, false})
        {
            auto const amend =
                withOptions ? features : features - featureOptions;
            Env env{*this, amend};
            auto const writer = Account("alice");
            auto const buyer = Account("bob");
            auto const gw = Account("gateway");
            auto const gme = Account("gme");
            auto const GME = gme["GME"];
            auto const USD = gw["USD"];

            env.fund(XRP(100'000), writer, buyer, gw, gme);
            env.close();
            env.trust(USD(100'000), writer, buyer);
            env.close();
            env(pay(gw, writer, USD(10'000)));
            env(pay(gw, buyer, USD(10'000)));
            env.close();
            env.trust(GME(100'000), writer, buyer);
            env.close();
            env(pay(gme, writer, GME(10'000)));
            env.close();

            auto const txResult =
                withOptions ? ter(tesSUCCESS) : ter(temDISABLED);

            // OPTION PAIR CREATE
            env(optionPairCreate(
                    writer,
                    STIssue(sfAsset, GME.issue()),
                    STIssue(sfAsset2, USD.issue())),
                fee(env.current()->fees().increment),
                txResult);
            env.close();

            // OPTION LIST
            auto const expiration = env.now() + 80s;
            auto const strikePrice = USD(20);
            std::int64_t const strike =
                static_cast<std::int64_t>(Number(strikePrice.value()));
            uint256 const optionId{
                getOptionIndex(gme.id(), GME.currency, strike, expiration)};
            auto const premium = USD(0.5);
            auto const quantity = 1000;
            auto const offerId = getOfferIndex(writer.id(), env.seq(writer));
            env(optionCreate(
                    writer,
                    expiration,
                    strikePrice,
                    STIssue(sfAsset, GME.issue()),
                    quantity,
                    premium),
                txResult);
            env.close();

            // OPTION EXERCISE
            env(optionSettle(writer, optionId, offerId),
                txflags(tfExercise),
                txResult);
            env.close();
        }
    }

    void
    testSettleInvalid(FeatureBitset features)
    {
        using namespace test::jtx;
        using namespace std::literals::chrono_literals;
        testcase("settle invalid");

        //----------------------------------------------------------------------
        // preflight

        // temINVALID_FLAG
        // temINVALID_FLAG

        //----------------------------------------------------------------------
        // preclaim

        // tecNO_ENTRY
        // tecNO_TARGET
        // tecNO_TARGET
        // tecNO_PERMISSION
        // tecNO_PERMISSION

        //----------------------------------------------------------------------
        // doApply.expire

        // tecINSUFFICIENT_FUNDS
        {
            Env env{*this, features};
            auto const writer = Account("alice");
            auto const buyer = Account("bob");
            auto const gw = Account("gateway");
            auto const gme = Account("gme");
            auto const GME = gme["GME"];
            auto const USD = gw["USD"];

            env.fund(XRP(100'000), writer, buyer, gw, gme);
            env.close();
            env.trust(USD(100'000), writer, buyer);
            env.close();
            env(pay(gw, writer, USD(10'000)));
            env(pay(gw, buyer, USD(10'000)));
            env.close();
            env.trust(GME(100'000), writer, buyer);
            env.close();
            env(pay(gme, writer, GME(10'000)));
            env.close();

            auto const expiration = env.now() + 80s;
            auto const strikePrice = USD(20);
            auto const premium = USD(0.5);
            auto const quantity = 1000;
            std::int64_t const strike =
                static_cast<std::int64_t>(Number(strikePrice.value()));
            uint256 const optionId{
                getOptionIndex(gme.id(), GME.currency, strike, expiration)};
            initPair(env, gme, GME.issue(), USD.issue());

            // create buy offer
            uint256 const buyId = createOffer(
                env,
                buyer,
                env.seq(buyer),
                GME,
                quantity,
                expiration,
                strikePrice.value(),
                premium.value(),
                0);

            // create sell offer
            createOffer(
                env,
                writer,
                env.seq(writer),
                GME,
                quantity,
                expiration,
                strikePrice.value(),
                premium.value(),
                tfSell);

            // expire sell offer
            env(optionSettle(buyer, optionId, buyId),
                txflags(tfExercise),
                ter(tecINSUFFICIENT_FUNDS));
            env.close();
        }

        //----------------------------------------------------------------------
        // doApply.close

        //----------------------------------------------------------------------
        // doApply.exercise
    }

    void
    testCreateBuyValid(FeatureBitset features)
    {
        using namespace test::jtx;
        using namespace std::literals::chrono_literals;
        testcase("create buy valid");

        // Create Buy / No Match
        {
            Env env{*this, features};
            auto const feeDrops = env.current()->fees().base;
            auto const writer = Account("alice");
            auto const buyer = Account("bob");
            auto const gw = Account("gateway");
            auto const gme = Account("gme");
            auto const GME = gme["GME"];
            auto const USD = gw["USD"];

            env.fund(XRP(100000), writer, buyer, gw, gme);
            env.close();
            env.trust(USD(100000), writer, buyer);
            env.close();
            env(pay(gw, writer, USD(10000)));
            env(pay(gw, buyer, USD(10000)));
            env.close();
            env.trust(GME(100000), writer, buyer);
            env.close();
            env(pay(gme, writer, GME(10000)));
            env.close();

            auto const preBuyerXrp = env.balance(buyer);
            auto const preBuyerGme = env.balance(buyer, GME);
            auto const preBuyerUsd = env.balance(buyer, USD);

            auto const expiration = env.now() + 1s;
            auto const strikePrice = USD(20);
            auto const premium = USD(0.5);
            auto const quantity = 1000;
            initPair(env, gme, GME.issue(), USD.issue());

            uint256 const buyId = createOffer(
                env,
                buyer,
                env.seq(buyer),
                GME,
                quantity,
                expiration,
                strikePrice.value(),
                premium.value(),
                tfMarket);

            // validate buy offer
            validateOffer(
                __LINE__,
                *env.current(),
                buyId,
                quantity,
                premium,
                GME(0).value(),
                1000,
                {});

            // check balances
            BEAST_EXPECT(env.balance(buyer) == preBuyerXrp - feeDrops);
            BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
            BEAST_EXPECT(env.balance(buyer, USD) == preBuyerUsd);

            // check metadata
            BEAST_EXPECT(inOwnerDir(*env.current(), buyer, buyId));
        }

        // Create Buy / Full Match
        {
            Env env{*this, features};
            auto const feeDrops = env.current()->fees().base;
            auto const writer = Account("alice");
            auto const buyer = Account("bob");
            auto const gw = Account("gateway");
            auto const gme = Account("gme");
            auto const GME = gme["GME"];
            auto const USD = gw["USD"];

            env.fund(XRP(100000), writer, buyer, gw, gme);
            env.close();
            env.trust(USD(100000), writer, buyer);
            env.close();
            env(pay(gw, writer, USD(10000)));
            env(pay(gw, buyer, USD(10000)));
            env.close();
            env.trust(GME(100000), writer, buyer);
            env.close();
            env(pay(gme, writer, GME(10000)));
            env.close();

            auto const preWriterXrp = env.balance(writer);
            auto const preWriterGme = env.balance(writer, GME);
            auto const preWriterUsd = env.balance(writer, USD);
            auto const preBuyerXrp = env.balance(buyer);
            auto const preBuyerGme = env.balance(buyer, GME);
            auto const preBuyerUsd = env.balance(buyer, USD);

            auto const expiration = env.now() + 1s;
            auto const strikePrice = USD(20);
            auto const premium = USD(0.5);
            auto const quantity = 1000;
            initPair(env, gme, GME.issue(), USD.issue());

            // create sell offer
            uint256 const sellId = createOffer(
                env,
                writer,
                env.seq(writer),
                GME,
                quantity,
                expiration,
                strikePrice.value(),
                premium.value(),
                tfSell);

            // validate sell offer
            validateOffer(
                __LINE__,
                *env.current(),
                sellId,
                quantity,
                premium,
                GME(1000).value(),
                1000,
                {});

            // check balances
            BEAST_EXPECT(env.balance(writer) == preWriterXrp - feeDrops);
            BEAST_EXPECT(env.balance(writer, GME) == preWriterGme - GME(1000));
            BEAST_EXPECT(env.balance(writer, USD) == preWriterUsd);
            BEAST_EXPECT(env.balance(buyer) == preBuyerXrp);
            BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
            BEAST_EXPECT(env.balance(buyer, USD) == preBuyerUsd);

            // check metadata
            BEAST_EXPECT(inOwnerDir(*env.current(), writer, sellId));

            // create buy offer
            uint256 const buyId = createOffer(
                env,
                buyer,
                env.seq(buyer),
                GME,
                quantity,
                expiration,
                strikePrice.value(),
                premium.value(),
                0);

            // validate buy offer
            validateOffer(
                __LINE__,
                *env.current(),
                buyId,
                quantity,
                premium,
                GME(0).value(),
                0,
                {{sellId, writer.id(), quantity}});

            // revalidate sell offer
            validateOffer(
                __LINE__,
                *env.current(),
                sellId,
                quantity,
                premium,
                GME(1000).value(),
                0,
                {{buyId, buyer.id(), quantity}});

            // check balances
            BEAST_EXPECT(env.balance(writer) == preWriterXrp - feeDrops);
            BEAST_EXPECT(env.balance(writer, GME) == preWriterGme - GME(1000));
            BEAST_EXPECT(env.balance(writer, USD) == preWriterUsd + USD(500));
            BEAST_EXPECT(env.balance(buyer) == preBuyerXrp - feeDrops);
            BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
            BEAST_EXPECT(env.balance(buyer, USD) == preBuyerUsd - USD(500));

            // check metadata
            BEAST_EXPECT(inOwnerDir(*env.current(), writer, sellId));
            BEAST_EXPECT(inOwnerDir(*env.current(), buyer, buyId));
        }

        // Create Buy / Partial Match
        {
            Env env{*this, features};
            auto const feeDrops = env.current()->fees().base;
            auto const writer = Account("alice");
            auto const buyer = Account("bob");
            auto const gw = Account("gateway");
            auto const gme = Account("gme");
            auto const GME = gme["GME"];
            auto const USD = gw["USD"];

            env.fund(XRP(100000), writer, buyer, gw, gme);
            env.close();
            env.trust(USD(100000), writer, buyer);
            env.close();
            env(pay(gw, writer, USD(10000)));
            env(pay(gw, buyer, USD(10000)));
            env.close();
            env.trust(GME(100000), writer, buyer);
            env.close();
            env(pay(gme, writer, GME(10000)));
            env.close();

            auto const preWriterXrp = env.balance(writer);
            auto const preWriterGme = env.balance(writer, GME);
            auto const preWriterUsd = env.balance(writer, USD);
            auto const preBuyerXrp = env.balance(buyer);
            auto const preBuyerGme = env.balance(buyer, GME);
            auto const preBuyerUsd = env.balance(buyer, USD);

            auto const expiration = env.now() + 1s;
            auto const strikePrice = USD(20);
            auto const premium = USD(0.5);
            initPair(env, gme, GME.issue(), USD.issue());

            // create sell offer
            uint256 const sellId = createOffer(
                env,
                writer,
                env.seq(writer),
                GME,
                1000,
                expiration,
                strikePrice.value(),
                premium.value(),
                tfSell);

            // validate sell offer
            validateOffer(
                __LINE__,
                *env.current(),
                sellId,
                1000,
                premium,
                GME(1000).value(),
                1000,
                {});

            // check balances
            BEAST_EXPECT(env.balance(writer) == preWriterXrp - feeDrops);
            BEAST_EXPECT(env.balance(writer, GME) == preWriterGme - GME(1000));
            BEAST_EXPECT(env.balance(writer, USD) == preWriterUsd);
            BEAST_EXPECT(env.balance(buyer) == preBuyerXrp);
            BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
            BEAST_EXPECT(env.balance(buyer, USD) == preBuyerUsd);

            // check metadata
            BEAST_EXPECT(inOwnerDir(*env.current(), writer, sellId));

            // create buy offer
            uint256 const buyId = createOffer(
                env,
                buyer,
                env.seq(buyer),
                GME,
                500,
                expiration,
                strikePrice.value(),
                premium.value(),
                0);

            // validate buy offer
            validateOffer(
                __LINE__,
                *env.current(),
                buyId,
                500,
                premium,
                GME(0).value(),
                0,
                {{sellId, writer.id(), 500}});

            // revalidate sell offer
            validateOffer(
                __LINE__,
                *env.current(),
                sellId,
                1000,
                premium,
                GME(1000).value(),
                500,
                {{buyId, buyer.id(), 500}});

            // check balances
            BEAST_EXPECT(env.balance(writer) == preWriterXrp - feeDrops);
            BEAST_EXPECT(env.balance(writer, GME) == preWriterGme - GME(1000));
            BEAST_EXPECT(env.balance(writer, USD) == preWriterUsd + USD(250));
            BEAST_EXPECT(env.balance(buyer) == preBuyerXrp - feeDrops);
            BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
            BEAST_EXPECT(env.balance(buyer, USD) == preBuyerUsd - USD(250));

            // check metadata
            BEAST_EXPECT(inOwnerDir(*env.current(), writer, sellId));
            BEAST_EXPECT(inOwnerDir(*env.current(), buyer, buyId));
        }
    }

    void
    testCreateSellValid(FeatureBitset features)
    {
        using namespace test::jtx;
        using namespace std::literals::chrono_literals;
        testcase("create sell valid");

        // Create Sell / No Match
        {
            Env env{*this, features};
            auto const feeDrops = env.current()->fees().base;
            auto const writer = Account("alice");
            auto const buyer = Account("bob");
            auto const gw = Account("gateway");
            auto const gme = Account("gme");
            auto const GME = gme["GME"];
            auto const USD = gw["USD"];

            env.fund(XRP(100000), writer, buyer, gw, gme);
            env.close();
            env.trust(USD(100000), writer, buyer);
            env.close();
            env(pay(gw, writer, USD(10000)));
            env(pay(gw, buyer, USD(10000)));
            env.close();
            env.trust(GME(100000), writer, buyer);
            env.close();
            env(pay(gme, writer, GME(10000)));
            env.close();

            auto const preWriterXrp = env.balance(writer);
            auto const preWriterGme = env.balance(writer, GME);
            auto const preWriterUsd = env.balance(writer, USD);

            auto const expiration = env.now() + 1s;
            auto const strikePrice = USD(20);
            auto const premium = USD(0.5);
            auto const quantity = 1000;
            initPair(env, gme, GME.issue(), USD.issue());

            uint256 const sellId = createOffer(
                env,
                writer,
                env.seq(writer),
                GME,
                quantity,
                expiration,
                strikePrice.value(),
                premium.value(),
                tfSell | tfMarket);

            // validate sell offer
            validateOffer(
                __LINE__,
                *env.current(),
                sellId,
                quantity,
                premium,
                GME(1000).value(),
                1000,
                {});

            // check balances
            BEAST_EXPECT(env.balance(writer) == preWriterXrp - feeDrops);
            BEAST_EXPECT(env.balance(writer, GME) == preWriterGme - GME(1000));
            BEAST_EXPECT(env.balance(writer, USD) == preWriterUsd);

            // check metadata
            BEAST_EXPECT(inOwnerDir(*env.current(), writer, sellId));
        }

        // Create Sell / Full Match
        {
            Env env{*this, features};
            auto const feeDrops = env.current()->fees().base;
            auto const writer = Account("alice");
            auto const buyer = Account("bob");
            auto const gw = Account("gateway");
            auto const gme = Account("gme");
            auto const GME = gme["GME"];
            auto const USD = gw["USD"];

            env.fund(XRP(100000), writer, buyer, gw, gme);
            env.close();
            env.trust(USD(100000), writer, buyer);
            env.close();
            env(pay(gw, writer, USD(10000)));
            env(pay(gw, buyer, USD(10000)));
            env.close();
            env.trust(GME(100000), writer, buyer);
            env.close();
            env(pay(gme, writer, GME(10000)));
            env.close();

            auto const preWriterXrp = env.balance(writer);
            auto const preWriterGme = env.balance(writer, GME);
            auto const preWriterUsd = env.balance(writer, USD);
            auto const preBuyerXrp = env.balance(buyer);
            auto const preBuyerGme = env.balance(buyer, GME);
            auto const preBuyerUsd = env.balance(buyer, USD);

            auto const expiration = env.now() + 1s;
            auto const strikePrice = USD(20);
            auto const premium = USD(0.5);
            auto const quantity = 1000;
            initPair(env, gme, GME.issue(), USD.issue());

            // create buy offer
            uint256 const buyId = createOffer(
                env,
                buyer,
                env.seq(buyer),
                GME,
                quantity,
                expiration,
                strikePrice.value(),
                premium.value(),
                tfMarket);

            // validate sell offer
            validateOffer(
                __LINE__,
                *env.current(),
                buyId,
                quantity,
                premium,
                USD(0).value(),
                1000,
                {});

            // check balances
            BEAST_EXPECT(env.balance(writer) == preWriterXrp);
            BEAST_EXPECT(env.balance(writer, GME) == preWriterGme);
            BEAST_EXPECT(env.balance(writer, USD) == preWriterUsd);
            BEAST_EXPECT(env.balance(buyer) == preBuyerXrp - feeDrops);
            BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
            BEAST_EXPECT(env.balance(buyer, USD) == preBuyerUsd);

            // check metadata
            BEAST_EXPECT(inOwnerDir(*env.current(), buyer, buyId));

            // create sell offer
            uint256 const sellId = createOffer(
                env,
                writer,
                env.seq(writer),
                GME,
                quantity,
                expiration,
                strikePrice.value(),
                premium.value(),
                tfSell | tfMarket);

            // validate sell offer
            validateOffer(
                __LINE__,
                *env.current(),
                sellId,
                quantity,
                premium,
                GME(1000).value(),
                0,
                {{buyId, buyer.id(), quantity}});

            // revalidate buy offer
            validateOffer(
                __LINE__,
                *env.current(),
                buyId,
                quantity,
                premium,
                USD(0).value(),
                0,
                {{sellId, writer.id(), quantity}});

            // check balances
            BEAST_EXPECT(env.balance(writer) == preWriterXrp - feeDrops);
            BEAST_EXPECT(env.balance(writer, GME) == preWriterGme - GME(1000));
            BEAST_EXPECT(env.balance(writer, USD) == preWriterUsd + USD(500));
            BEAST_EXPECT(env.balance(buyer) == preBuyerXrp - feeDrops);
            BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
            BEAST_EXPECT(env.balance(buyer, USD) == preBuyerUsd - USD(500));

            // check metadata
            BEAST_EXPECT(inOwnerDir(*env.current(), writer, sellId));
            BEAST_EXPECT(inOwnerDir(*env.current(), buyer, buyId));
        }

        // Create Sell / Partial Match
        {
            Env env{*this, features};
            auto const feeDrops = env.current()->fees().base;
            auto const writer = Account("alice");
            auto const buyer = Account("bob");
            auto const gw = Account("gateway");
            auto const gme = Account("gme");
            auto const GME = gme["GME"];
            auto const USD = gw["USD"];

            env.fund(XRP(100000), writer, buyer, gw, gme);
            env.close();
            env.trust(USD(100000), writer, buyer);
            env.close();
            env(pay(gw, writer, USD(10000)));
            env(pay(gw, buyer, USD(10000)));
            env.close();
            env.trust(GME(100000), writer, buyer);
            env.close();
            env(pay(gme, writer, GME(10000)));
            env.close();

            auto const preWriterXrp = env.balance(writer);
            auto const preWriterGme = env.balance(writer, GME);
            auto const preWriterUsd = env.balance(writer, USD);
            auto const preBuyerXrp = env.balance(buyer);
            auto const preBuyerGme = env.balance(buyer, GME);
            auto const preBuyerUsd = env.balance(buyer, USD);

            auto const expiration = env.now() + 1s;
            auto const strikePrice = USD(20);
            auto const premium = USD(0.5);
            initPair(env, gme, GME.issue(), USD.issue());

            // create buy offer
            uint256 const buyId = createOffer(
                env,
                buyer,
                env.seq(buyer),
                GME,
                1000,
                expiration,
                strikePrice.value(),
                premium.value(),
                tfMarket);

            // validate sell offer
            validateOffer(
                __LINE__,
                *env.current(),
                buyId,
                1000,
                premium,
                USD(0).value(),
                1000,
                {});

            // check balances
            BEAST_EXPECT(env.balance(writer) == preWriterXrp);
            BEAST_EXPECT(env.balance(writer, GME) == preWriterGme);
            BEAST_EXPECT(env.balance(writer, USD) == preWriterUsd);
            BEAST_EXPECT(env.balance(buyer) == preBuyerXrp - feeDrops);
            BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
            BEAST_EXPECT(env.balance(buyer, USD) == preBuyerUsd);

            // check metadata
            BEAST_EXPECT(inOwnerDir(*env.current(), buyer, buyId));

            // create sell offer
            uint256 const sellId = createOffer(
                env,
                writer,
                env.seq(writer),
                GME,
                500,
                expiration,
                strikePrice.value(),
                premium.value(),
                tfSell | tfMarket);

            // validate sell offer
            validateOffer(
                __LINE__,
                *env.current(),
                sellId,
                500,
                premium,
                GME(500).value(),
                0,
                {{buyId, buyer.id(), 500}});

            // revalidate buy offer
            validateOffer(
                __LINE__,
                *env.current(),
                buyId,
                1000,
                premium,
                USD(0).value(),
                500,
                {{sellId, writer.id(), 500}});

            // check balances
            BEAST_EXPECT(env.balance(writer) == preWriterXrp - feeDrops);
            BEAST_EXPECT(env.balance(writer, GME) == preWriterGme - GME(500));
            BEAST_EXPECT(env.balance(writer, USD) == preWriterUsd + USD(250));
            BEAST_EXPECT(env.balance(buyer) == preBuyerXrp - feeDrops);
            BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
            BEAST_EXPECT(env.balance(buyer, USD) == preBuyerUsd - USD(250));

            // check metadata
            BEAST_EXPECT(inOwnerDir(*env.current(), writer, sellId));
            BEAST_EXPECT(inOwnerDir(*env.current(), buyer, buyId));
        }
    }

    void
    testCloseBuyCall(FeatureBitset features)
    {
        using namespace test::jtx;
        using namespace std::literals::chrono_literals;
        testcase("close buy call");

        Env env{*this, features};
        auto const feeDrops = env.current()->fees().base;
        auto const writer = Account("writer");
        auto const counter = Account("counter");
        auto const buyer = Account("buyer");
        auto const gw = Account("gateway");
        auto const gme = Account("gme");
        auto const GME = gme["GME"];
        auto const USD = gw["USD"];

        env.fund(XRP(1'000'000), writer, counter, buyer, gw, gme);
        env.close();
        env.trust(USD(1'000'000), writer, counter, buyer);
        env.close();
        env(pay(gw, writer, USD(100'000)));
        env(pay(gw, counter, USD(100'000)));
        env(pay(gw, buyer, USD(100'000)));
        env.close();
        env.trust(GME(100000), writer, counter, buyer);
        env.close();
        env(pay(gme, writer, GME(10'000)));
        env(pay(gme, counter, GME(10'000)));
        env.close();

        auto const preWriterXrp = env.balance(writer);
        auto const preWriterGme = env.balance(writer, GME);
        auto const preWriterUsd = env.balance(writer, USD);
        auto const preBuyerXrp = env.balance(buyer);
        auto const preBuyerGme = env.balance(buyer, GME);
        auto const preBuyerUsd = env.balance(buyer, USD);
        auto const preCounterXrp = env.balance(counter);
        auto const preCounterGme = env.balance(counter, GME);
        auto const preCounterUsd = env.balance(counter, USD);

        auto const expiration = env.now() + 80s;
        auto const strikePrice = USD(20);
        std::int64_t const strike =
            static_cast<std::int64_t>(Number(strikePrice.value()));
        uint256 const optionId{
            getOptionIndex(gme.id(), GME.currency, strike, expiration)};
        initPair(env, gme, GME.issue(), USD.issue());

        auto const premium = USD(0.5);

        // create sell offer
        uint256 const sellId = createOffer(
            env,
            writer,
            env.seq(writer),
            GME,
            1000,
            expiration,
            strikePrice.value(),
            premium.value(),
            tfSell | tfMarket);

        // validate sell offer
        validateOffer(
            __LINE__,
            *env.current(),
            sellId,
            1000,
            premium,
            GME(1000).value(),
            1000,
            {});

        // check balances
        BEAST_EXPECT(env.balance(writer) == preWriterXrp - feeDrops);
        BEAST_EXPECT(env.balance(writer, GME) == preWriterGme - GME(1000));
        BEAST_EXPECT(env.balance(writer, USD) == preWriterUsd);
        BEAST_EXPECT(env.balance(buyer) == preBuyerXrp);
        BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
        BEAST_EXPECT(env.balance(buyer, USD) == preBuyerUsd);
        BEAST_EXPECT(env.balance(counter) == preCounterXrp);
        BEAST_EXPECT(env.balance(counter, GME) == preCounterGme);
        BEAST_EXPECT(env.balance(counter, USD) == preCounterUsd);

        // check meta data
        BEAST_EXPECT(inOwnerDir(*env.current(), writer, sellId));

        // create buy offer
        uint256 const buyId = createOffer(
            env,
            buyer,
            env.seq(buyer),
            GME,
            500,
            expiration,
            strikePrice.value(),
            premium.value(),
            tfMarket);

        // validate buy offer
        validateOffer(
            __LINE__,
            *env.current(),
            buyId,
            500,
            premium,
            GME(0).value(),
            0,
            {{sellId, writer.id(), 500}});

        // validate sell offer
        validateOffer(
            __LINE__,
            *env.current(),
            sellId,
            1000,
            premium,
            GME(1000).value(),
            500,
            {{buyId, buyer.id(), 500}});

        // check balances
        BEAST_EXPECT(env.balance(writer) == preWriterXrp - feeDrops);
        BEAST_EXPECT(env.balance(writer, GME) == preWriterGme - GME(1000));
        BEAST_EXPECT(env.balance(writer, USD) == preWriterUsd + USD(250));
        BEAST_EXPECT(env.balance(buyer) == preBuyerXrp - feeDrops);
        BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
        BEAST_EXPECT(env.balance(buyer, USD) == preBuyerUsd - USD(250));
        BEAST_EXPECT(env.balance(counter) == preCounterXrp);
        BEAST_EXPECT(env.balance(counter, GME) == preCounterGme);
        BEAST_EXPECT(env.balance(counter, USD) == preCounterUsd);

        // check meta data
        BEAST_EXPECT(inOwnerDir(*env.current(), writer, sellId));
        BEAST_EXPECT(inOwnerDir(*env.current(), buyer, buyId));

        // create buy (counter) offer
        uint256 const counterId = createOffer(
            env,
            counter,
            env.seq(counter),
            GME,
            1000,
            expiration,
            strikePrice.value(),
            USD(0.2).value(),
            tfMarket);

        // validate counter offer
        validateOffer(
            __LINE__,
            *env.current(),
            counterId,
            1000,
            USD(0.2),
            GME(0).value(),
            500,
            {{sellId, writer.id(), 500}});

        // check balances
        BEAST_EXPECT(env.balance(writer) == preWriterXrp - feeDrops);
        BEAST_EXPECT(env.balance(writer, GME) == preWriterGme - GME(1000));
        BEAST_EXPECT(
            env.balance(writer, USD) == preWriterUsd + USD(250) + USD(250));
        BEAST_EXPECT(env.balance(buyer) == preBuyerXrp - feeDrops);
        BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
        BEAST_EXPECT(env.balance(buyer, USD) == preBuyerUsd - USD(250));
        BEAST_EXPECT(env.balance(counter) == preCounterXrp - feeDrops);
        BEAST_EXPECT(env.balance(counter, GME) == preCounterGme);
        BEAST_EXPECT(env.balance(counter, USD) == preCounterUsd - USD(250));

        // check meta data
        BEAST_EXPECT(inOwnerDir(*env.current(), writer, sellId));
        BEAST_EXPECT(inOwnerDir(*env.current(), buyer, buyId));
        BEAST_EXPECT(inOwnerDir(*env.current(), counter, counterId));

        // exercise buy offer
        env(optionSettle(buyer, optionId, buyId),
            txflags(tfClose),
            ter(tesSUCCESS));
        env.close();

        // validate counter offer
        validateOffer(
            __LINE__,
            *env.current(),
            counterId,
            1000,
            USD(0.2),
            GME(0).value(),
            0,
            {{sellId, writer.id(), 500}, {sellId, writer.id(), 500}});

        // validate sell offer
        validateOffer(
            __LINE__,
            *env.current(),
            sellId,
            1000,
            premium,
            GME(1000).value(),
            0,
            {{counterId, counter.id(), 500}, {counterId, counter.id(), 500}});

        // check balances
        BEAST_EXPECT(env.balance(writer) == preWriterXrp - feeDrops);
        BEAST_EXPECT(env.balance(writer, GME) == preWriterGme - GME(1000));
        BEAST_EXPECT(
            env.balance(writer, USD) == preWriterUsd + USD(250) + USD(250));
        BEAST_EXPECT(env.balance(buyer) == preBuyerXrp - (feeDrops * 2));
        BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
        BEAST_EXPECT(
            env.balance(buyer, USD) == preBuyerUsd - USD(250) + USD(100));
        BEAST_EXPECT(env.balance(counter) == preCounterXrp - feeDrops);
        BEAST_EXPECT(env.balance(counter, GME) == preCounterGme);
        BEAST_EXPECT(
            env.balance(counter, USD) == preCounterUsd - USD(250) - USD(100));

        // check meta data
        BEAST_EXPECT(inOwnerDir(*env.current(), writer, sellId));
        BEAST_EXPECT(!inOwnerDir(*env.current(), buyer, buyId));
        BEAST_EXPECT(inOwnerDir(*env.current(), counter, counterId));
    }

    void
    testCloseBuyPut(FeatureBitset features)
    {
        using namespace test::jtx;
        using namespace std::literals::chrono_literals;
        testcase("close buy put");

        Env env{*this, features};
        auto const feeDrops = env.current()->fees().base;
        auto const writer = Account("writer");
        auto const counter = Account("counter");
        auto const buyer = Account("buyer");
        auto const gw = Account("gateway");
        auto const gme = Account("gme");
        auto const GME = gme["GME"];
        auto const USD = gw["USD"];

        env.fund(XRP(1'000'000), writer, counter, buyer, gw, gme);
        env.close();
        env.trust(USD(1'000'000), writer, counter, buyer);
        env.close();
        env(pay(gw, writer, USD(100'000)));
        env(pay(gw, counter, USD(100'000)));
        env(pay(gw, buyer, USD(100'000)));
        env.close();
        env.trust(GME(100000), writer, counter, buyer);
        env.close();
        env(pay(gme, writer, GME(10'000)));
        env(pay(gme, counter, GME(10'000)));
        env.close();

        auto const preWriterXrp = env.balance(writer);
        auto const preWriterGme = env.balance(writer, GME);
        auto const preWriterUsd = env.balance(writer, USD);
        auto const preBuyerXrp = env.balance(buyer);
        auto const preBuyerGme = env.balance(buyer, GME);
        auto const preBuyerUsd = env.balance(buyer, USD);
        auto const preCounterXrp = env.balance(counter);
        auto const preCounterGme = env.balance(counter, GME);
        auto const preCounterUsd = env.balance(counter, USD);

        auto const expiration = env.now() + 80s;
        auto const strikePrice = USD(20);
        std::int64_t const strike =
            static_cast<std::int64_t>(Number(strikePrice.value()));
        uint256 const optionId{
            getOptionIndex(gme.id(), GME.currency, strike, expiration)};
        initPair(env, gme, GME.issue(), USD.issue());

        auto const premium = USD(0.5);

        // create sell offer
        uint256 const sellId = createOffer(
            env,
            writer,
            env.seq(writer),
            GME,
            1000,
            expiration,
            strikePrice.value(),
            premium.value(),
            tfSell | tfMarket | tfPut);

        // validate sell offer
        validateOffer(
            __LINE__,
            *env.current(),
            sellId,
            1000,
            premium,
            USD(20'000).value(),
            1000,
            {});

        // check balances
        BEAST_EXPECT(env.balance(writer) == preWriterXrp - feeDrops);
        BEAST_EXPECT(env.balance(writer, GME) == preWriterGme);
        BEAST_EXPECT(
            env.balance(writer, USD) == preWriterUsd - USD(strike * 1000));
        BEAST_EXPECT(env.balance(buyer) == preBuyerXrp);
        BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
        BEAST_EXPECT(env.balance(buyer, USD) == preBuyerUsd);
        BEAST_EXPECT(env.balance(counter) == preCounterXrp);
        BEAST_EXPECT(env.balance(counter, GME) == preCounterGme);
        BEAST_EXPECT(env.balance(counter, USD) == preCounterUsd);

        // check meta data
        BEAST_EXPECT(inOwnerDir(*env.current(), writer, sellId));

        // create buy offer
        uint256 const buyId = createOffer(
            env,
            buyer,
            env.seq(buyer),
            GME,
            500,
            expiration,
            strikePrice.value(),
            premium.value(),
            tfMarket | tfPut);

        // validate buy offer
        validateOffer(
            __LINE__,
            *env.current(),
            buyId,
            500,
            premium,
            GME(0).value(),
            0,
            {{sellId, writer.id(), 500}});

        // validate sell offer
        validateOffer(
            __LINE__,
            *env.current(),
            sellId,
            1000,
            premium,
            USD(20'000).value(),
            500,
            {{buyId, buyer.id(), 500}});

        // check balances
        BEAST_EXPECT(env.balance(writer) == preWriterXrp - feeDrops);
        BEAST_EXPECT(env.balance(writer, GME) == preWriterGme);
        BEAST_EXPECT(
            env.balance(writer, USD) ==
            preWriterUsd - USD(strike * 1000) + USD(250));
        BEAST_EXPECT(env.balance(buyer) == preBuyerXrp - feeDrops);
        BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
        BEAST_EXPECT(env.balance(buyer, USD) == preBuyerUsd - USD(250));
        BEAST_EXPECT(env.balance(counter) == preCounterXrp);
        BEAST_EXPECT(env.balance(counter, GME) == preCounterGme);
        BEAST_EXPECT(env.balance(counter, USD) == preCounterUsd);

        // check meta data
        BEAST_EXPECT(inOwnerDir(*env.current(), writer, sellId));
        BEAST_EXPECT(inOwnerDir(*env.current(), buyer, buyId));

        // create buy (counter) offer
        uint256 const counterId = createOffer(
            env,
            counter,
            env.seq(counter),
            GME,
            1000,
            expiration,
            strikePrice.value(),
            USD(5.2).value(),
            tfMarket | tfPut);

        // validate counter offer
        validateOffer(
            __LINE__,
            *env.current(),
            counterId,
            1000,
            USD(5.2),
            GME(0).value(),
            500,
            {{sellId, writer.id(), 500}});

        // check balances
        BEAST_EXPECT(env.balance(writer) == preWriterXrp - feeDrops);
        BEAST_EXPECT(env.balance(writer, GME) == preWriterGme);
        BEAST_EXPECT(
            env.balance(writer, USD) ==
            preWriterUsd - USD(strike * 1000) + USD(250) + USD(250));
        BEAST_EXPECT(env.balance(buyer) == preBuyerXrp - feeDrops);
        BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
        BEAST_EXPECT(env.balance(buyer, USD) == preBuyerUsd - USD(250));
        BEAST_EXPECT(env.balance(counter) == preCounterXrp - feeDrops);
        BEAST_EXPECT(env.balance(counter, GME) == preCounterGme);
        BEAST_EXPECT(env.balance(counter, USD) == preCounterUsd - USD(250));

        // check meta data
        BEAST_EXPECT(inOwnerDir(*env.current(), writer, sellId));
        BEAST_EXPECT(inOwnerDir(*env.current(), buyer, buyId));
        BEAST_EXPECT(inOwnerDir(*env.current(), counter, counterId));

        // exercise buy offer
        env(optionSettle(buyer, optionId, buyId),
            txflags(tfClose),
            ter(tesSUCCESS));
        env.close();

        // validate counter offer
        validateOffer(
            __LINE__,
            *env.current(),
            counterId,
            1000,
            USD(5.2),
            GME(0).value(),
            0,
            {{sellId, writer.id(), 500}, {sellId, writer.id(), 500}});

        // validate sell offer
        validateOffer(
            __LINE__,
            *env.current(),
            sellId,
            1000,
            premium,
            USD(20'000).value(),
            0,
            {{counterId, counter.id(), 500}, {counterId, counter.id(), 500}});

        // check balances
        BEAST_EXPECT(env.balance(writer) == preWriterXrp - feeDrops);
        BEAST_EXPECT(env.balance(writer, GME) == preWriterGme);
        BEAST_EXPECT(
            env.balance(writer, USD) ==
            preWriterUsd - USD(strike * 1000) + USD(250) + USD(250));
        BEAST_EXPECT(env.balance(buyer) == preBuyerXrp - (feeDrops * 2));
        BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
        BEAST_EXPECT(
            env.balance(buyer, USD) == preBuyerUsd - USD(250) + USD(2600));
        BEAST_EXPECT(env.balance(counter) == preCounterXrp - feeDrops);
        BEAST_EXPECT(env.balance(counter, GME) == preCounterGme);
        BEAST_EXPECT(
            env.balance(counter, USD) == preCounterUsd - USD(250) - USD(2600));

        // check meta data
        BEAST_EXPECT(inOwnerDir(*env.current(), writer, sellId));
        BEAST_EXPECT(!inOwnerDir(*env.current(), buyer, buyId));
        BEAST_EXPECT(inOwnerDir(*env.current(), counter, counterId));
    }

    void
    testCloseSellCall(FeatureBitset features)
    {
        using namespace test::jtx;
        using namespace std::literals::chrono_literals;
        testcase("close sell call");

        Env env{*this, features};
        auto const feeDrops = env.current()->fees().base;
        auto const writer = Account("writer");
        auto const counter = Account("counter");
        auto const buyer = Account("buyer");
        auto const gw = Account("gateway");
        auto const gme = Account("gme");
        auto const GME = gme["GME"];
        auto const USD = gw["USD"];

        env.fund(XRP(1'000'000), writer, counter, buyer, gw, gme);
        env.close();
        env.trust(USD(1'000'000), writer, counter, buyer);
        env.close();
        env(pay(gw, writer, USD(100'000)));
        env(pay(gw, counter, USD(100'000)));
        env(pay(gw, buyer, USD(100'000)));
        env.close();
        env.trust(GME(100000), writer, counter, buyer);
        env.close();
        env(pay(gme, writer, GME(10'000)));
        env(pay(gme, counter, GME(10'000)));
        env.close();

        auto const preWriterXrp = env.balance(writer);
        auto const preWriterGme = env.balance(writer, GME);
        auto const preWriterUsd = env.balance(writer, USD);
        auto const preBuyerXrp = env.balance(buyer);
        auto const preBuyerGme = env.balance(buyer, GME);
        auto const preBuyerUsd = env.balance(buyer, USD);
        auto const preCounterXrp = env.balance(counter);
        auto const preCounterGme = env.balance(counter, GME);
        auto const preCounterUsd = env.balance(counter, USD);

        auto const expiration = env.now() + 80s;
        auto const strikePrice = USD(20);
        std::int64_t const strike =
            static_cast<std::int64_t>(Number(strikePrice.value()));
        uint256 const optionId{
            getOptionIndex(gme.id(), GME.currency, strike, expiration)};
        initPair(env, gme, GME.issue(), USD.issue());

        auto const premium = USD(0.5);

        // create sell offer
        uint256 const sellId = createOffer(
            env,
            writer,
            env.seq(writer),
            GME,
            500,
            expiration,
            strikePrice.value(),
            premium.value(),
            tfSell | tfMarket);

        // validate sell offer
        validateOffer(
            __LINE__,
            *env.current(),
            sellId,
            500,
            premium,
            GME(500).value(),
            500,
            {});

        // check balances
        BEAST_EXPECT(env.balance(writer) == preWriterXrp - feeDrops);
        BEAST_EXPECT(env.balance(writer, GME) == preWriterGme - GME(500));
        BEAST_EXPECT(env.balance(writer, USD) == preWriterUsd);
        BEAST_EXPECT(env.balance(buyer) == preBuyerXrp);
        BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
        BEAST_EXPECT(env.balance(buyer, USD) == preBuyerUsd);
        BEAST_EXPECT(env.balance(counter) == preCounterXrp);
        BEAST_EXPECT(env.balance(counter, GME) == preCounterGme);
        BEAST_EXPECT(env.balance(counter, USD) == preCounterUsd);

        // check meta data
        BEAST_EXPECT(inOwnerDir(*env.current(), writer, sellId));

        // create buy offer
        uint256 const buyId = createOffer(
            env,
            buyer,
            env.seq(buyer),
            GME,
            1000,
            expiration,
            strikePrice.value(),
            premium.value(),
            tfMarket);

        // validate buy offer
        validateOffer(
            __LINE__,
            *env.current(),
            buyId,
            1000,
            premium,
            GME(0).value(),
            500,
            {{sellId, writer.id(), 500}});

        // validate sell offer
        validateOffer(
            __LINE__,
            *env.current(),
            sellId,
            500,
            premium,
            GME(500).value(),
            0,
            {{buyId, buyer.id(), 500}});

        // check balances
        BEAST_EXPECT(env.balance(writer) == preWriterXrp - feeDrops);
        BEAST_EXPECT(env.balance(writer, GME) == preWriterGme - GME(500));
        BEAST_EXPECT(env.balance(writer, USD) == preWriterUsd + USD(250));
        BEAST_EXPECT(env.balance(buyer) == preBuyerXrp - feeDrops);
        BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
        BEAST_EXPECT(env.balance(buyer, USD) == preBuyerUsd - USD(250));
        BEAST_EXPECT(env.balance(counter) == preCounterXrp);
        BEAST_EXPECT(env.balance(counter, GME) == preCounterGme);
        BEAST_EXPECT(env.balance(counter, USD) == preCounterUsd);

        // check meta data
        BEAST_EXPECT(inOwnerDir(*env.current(), writer, sellId));
        BEAST_EXPECT(inOwnerDir(*env.current(), buyer, buyId));

        // create buy (counter) offer
        uint256 const counterId = createOffer(
            env,
            counter,
            env.seq(counter),
            GME,
            1000,
            expiration,
            strikePrice.value(),
            USD(0.2).value(),
            tfSell | tfMarket);

        // validate counter offer
        validateOffer(
            __LINE__,
            *env.current(),
            counterId,
            1000,
            USD(0.2),
            GME(1000).value(),
            500,
            {{buyId, buyer.id(), 500}});

        // check balances
        BEAST_EXPECT(env.balance(writer) == preWriterXrp - feeDrops);
        BEAST_EXPECT(env.balance(writer, GME) == preWriterGme - GME(500));
        BEAST_EXPECT(env.balance(writer, USD) == preWriterUsd + USD(250));
        BEAST_EXPECT(env.balance(buyer) == preBuyerXrp - feeDrops);
        BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
        BEAST_EXPECT(
            env.balance(buyer, USD) == preBuyerUsd - USD(250) - USD(250));
        BEAST_EXPECT(env.balance(counter) == preCounterXrp - feeDrops);
        BEAST_EXPECT(env.balance(counter, GME) == preCounterGme - GME(1000));
        BEAST_EXPECT(env.balance(counter, USD) == preCounterUsd + USD(250));

        // check meta data
        BEAST_EXPECT(inOwnerDir(*env.current(), writer, sellId));
        BEAST_EXPECT(inOwnerDir(*env.current(), buyer, buyId));
        BEAST_EXPECT(inOwnerDir(*env.current(), counter, counterId));

        // exercise buy offer
        env(optionSettle(writer, optionId, sellId),
            txflags(tfClose),
            ter(tesSUCCESS));
        env.close();

        // validate counter offer
        validateOffer(
            __LINE__,
            *env.current(),
            counterId,
            1000,
            USD(0.2),
            GME(1000).value(),
            0,
            {{buyId, buyer.id(), 500}, {buyId, buyer.id(), 500}});

        // validate buy offer
        validateOffer(
            __LINE__,
            *env.current(),
            buyId,
            1000,
            premium,
            GME(0).value(),
            0,
            {{counterId, counter.id(), 500}, {counterId, counter.id(), 500}});

        // check balances
        BEAST_EXPECT(env.balance(writer) == preWriterXrp - (feeDrops * 2));
        BEAST_EXPECT(env.balance(writer, GME) == preWriterGme);
        BEAST_EXPECT(env.balance(writer, USD) == preWriterUsd + USD(250));
        BEAST_EXPECT(env.balance(buyer) == preBuyerXrp - feeDrops);
        BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
        BEAST_EXPECT(
            env.balance(buyer, USD) == preBuyerUsd - USD(250) - USD(250));
        BEAST_EXPECT(env.balance(counter) == preCounterXrp - feeDrops);
        BEAST_EXPECT(env.balance(counter, GME) == preCounterGme - GME(1000));
        BEAST_EXPECT(env.balance(counter, USD) == preCounterUsd + USD(250));

        // check meta data
        BEAST_EXPECT(!inOwnerDir(*env.current(), writer, sellId));
        BEAST_EXPECT(inOwnerDir(*env.current(), buyer, buyId));
        BEAST_EXPECT(inOwnerDir(*env.current(), counter, counterId));
    }

    void
    testCloseSellPut(FeatureBitset features)
    {
        using namespace test::jtx;
        using namespace std::literals::chrono_literals;
        testcase("close sell put");

        Env env{*this, features};
        auto const feeDrops = env.current()->fees().base;
        auto const writer = Account("writer");
        auto const counter = Account("counter");
        auto const buyer = Account("buyer");
        auto const gw = Account("gateway");
        auto const gme = Account("gme");
        auto const GME = gme["GME"];
        auto const USD = gw["USD"];

        env.fund(XRP(1'000'000), writer, counter, buyer, gw, gme);
        env.close();
        env.trust(USD(1'000'000), writer, counter, buyer);
        env.close();
        env(pay(gw, writer, USD(100'000)));
        env(pay(gw, counter, USD(100'000)));
        env(pay(gw, buyer, USD(100'000)));
        env.close();
        env.trust(GME(100000), writer, counter, buyer);
        env.close();
        env(pay(gme, writer, GME(10'000)));
        env(pay(gme, counter, GME(10'000)));
        env.close();

        auto const preWriterXrp = env.balance(writer);
        auto const preWriterGme = env.balance(writer, GME);
        auto const preWriterUsd = env.balance(writer, USD);
        auto const preBuyerXrp = env.balance(buyer);
        auto const preBuyerGme = env.balance(buyer, GME);
        auto const preBuyerUsd = env.balance(buyer, USD);
        auto const preCounterXrp = env.balance(counter);
        auto const preCounterGme = env.balance(counter, GME);
        auto const preCounterUsd = env.balance(counter, USD);

        auto const expiration = env.now() + 80s;
        auto const strikePrice = USD(20);
        std::int64_t const strike =
            static_cast<std::int64_t>(Number(strikePrice.value()));
        uint256 const optionId{
            getOptionIndex(gme.id(), GME.currency, strike, expiration)};
        initPair(env, gme, GME.issue(), USD.issue());

        auto const premium = USD(0.5);

        // create sell offer
        uint256 const sellId = createOffer(
            env,
            writer,
            env.seq(writer),
            GME,
            500,
            expiration,
            strikePrice.value(),
            premium.value(),
            tfSell | tfMarket | tfPut);

        // validate sell offer
        validateOffer(
            __LINE__,
            *env.current(),
            sellId,
            500,
            premium,
            USD(10'000).value(),
            500,
            {});

        // check balances
        BEAST_EXPECT(env.balance(writer) == preWriterXrp - feeDrops);
        BEAST_EXPECT(env.balance(writer, GME) == preWriterGme);
        BEAST_EXPECT(env.balance(writer, USD) == preWriterUsd - USD(10'000));
        BEAST_EXPECT(env.balance(buyer) == preBuyerXrp);
        BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
        BEAST_EXPECT(env.balance(buyer, USD) == preBuyerUsd);
        BEAST_EXPECT(env.balance(counter) == preCounterXrp);
        BEAST_EXPECT(env.balance(counter, GME) == preCounterGme);
        BEAST_EXPECT(env.balance(counter, USD) == preCounterUsd);

        // check meta data
        BEAST_EXPECT(inOwnerDir(*env.current(), writer, sellId));

        // create buy offer
        uint256 const buyId = createOffer(
            env,
            buyer,
            env.seq(buyer),
            GME,
            1000,
            expiration,
            strikePrice.value(),
            premium.value(),
            tfMarket | tfPut);

        // validate buy offer
        validateOffer(
            __LINE__,
            *env.current(),
            buyId,
            1000,
            premium,
            GME(0).value(),
            500,
            {{sellId, writer.id(), 500}});

        // validate sell offer
        validateOffer(
            __LINE__,
            *env.current(),
            sellId,
            500,
            premium,
            USD(10'000).value(),
            0,
            {{buyId, buyer.id(), 500}});

        // check balances
        BEAST_EXPECT(env.balance(writer) == preWriterXrp - feeDrops);
        BEAST_EXPECT(env.balance(writer, GME) == preWriterGme);
        BEAST_EXPECT(
            env.balance(writer, USD) == preWriterUsd - USD(10'000) + USD(250));
        BEAST_EXPECT(env.balance(buyer) == preBuyerXrp - feeDrops);
        BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
        BEAST_EXPECT(env.balance(buyer, USD) == preBuyerUsd - USD(250));
        BEAST_EXPECT(env.balance(counter) == preCounterXrp);
        BEAST_EXPECT(env.balance(counter, GME) == preCounterGme);
        BEAST_EXPECT(env.balance(counter, USD) == preCounterUsd);

        // check meta data
        BEAST_EXPECT(inOwnerDir(*env.current(), writer, sellId));
        BEAST_EXPECT(inOwnerDir(*env.current(), buyer, buyId));

        // create buy (counter) offer
        uint256 const counterId = createOffer(
            env,
            counter,
            env.seq(counter),
            GME,
            1000,
            expiration,
            strikePrice.value(),
            USD(0.2).value(),
            tfSell | tfMarket | tfPut);

        // validate counter offer
        validateOffer(
            __LINE__,
            *env.current(),
            counterId,
            1000,
            USD(0.2),
            USD(20'000).value(),
            500,
            {{buyId, buyer.id(), 500}});

        // check balances
        BEAST_EXPECT(env.balance(writer) == preWriterXrp - feeDrops);
        BEAST_EXPECT(env.balance(writer, GME) == preWriterGme);
        BEAST_EXPECT(
            env.balance(writer, USD) == preWriterUsd - USD(10'000) + USD(250));
        BEAST_EXPECT(env.balance(buyer) == preBuyerXrp - feeDrops);
        BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
        BEAST_EXPECT(
            env.balance(buyer, USD) == preBuyerUsd - USD(250) - USD(250));
        BEAST_EXPECT(env.balance(counter) == preCounterXrp - feeDrops);
        BEAST_EXPECT(env.balance(counter, GME) == preCounterGme);
        BEAST_EXPECT(
            env.balance(counter, USD) ==
            preCounterUsd - USD(20'000) + USD(250));

        // check meta data
        BEAST_EXPECT(inOwnerDir(*env.current(), writer, sellId));
        BEAST_EXPECT(inOwnerDir(*env.current(), buyer, buyId));
        BEAST_EXPECT(inOwnerDir(*env.current(), counter, counterId));

        // exercise buy offer
        env(optionSettle(writer, optionId, sellId),
            txflags(tfClose),
            ter(tesSUCCESS));
        env.close();

        // validate counter offer
        validateOffer(
            __LINE__,
            *env.current(),
            counterId,
            1000,
            USD(0.2),
            USD(20'000).value(),
            0,
            {{buyId, buyer.id(), 500}, {buyId, buyer.id(), 500}});

        // validate buy offer
        validateOffer(
            __LINE__,
            *env.current(),
            buyId,
            1000,
            premium,
            GME(0).value(),
            0,
            {{counterId, counter.id(), 500}, {counterId, counter.id(), 500}});

        // check balances
        BEAST_EXPECT(env.balance(writer) == preWriterXrp - (feeDrops * 2));
        BEAST_EXPECT(env.balance(writer, GME) == preWriterGme);
        BEAST_EXPECT(env.balance(writer, USD) == preWriterUsd + USD(250));
        BEAST_EXPECT(env.balance(buyer) == preBuyerXrp - feeDrops);
        BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
        BEAST_EXPECT(
            env.balance(buyer, USD) == preBuyerUsd - USD(250) - USD(250));
        BEAST_EXPECT(env.balance(counter) == preCounterXrp - feeDrops);
        BEAST_EXPECT(env.balance(counter, GME) == preCounterGme);
        BEAST_EXPECT(
            env.balance(counter, USD) ==
            preCounterUsd - USD(20'000) + USD(250));

        // check meta data
        BEAST_EXPECT(!inOwnerDir(*env.current(), writer, sellId));
        BEAST_EXPECT(inOwnerDir(*env.current(), buyer, buyId));
        BEAST_EXPECT(inOwnerDir(*env.current(), counter, counterId));
    }

    void
    testExerciseCall(FeatureBitset features)
    {
        using namespace test::jtx;
        using namespace std::literals::chrono_literals;
        testcase("exercise call");

        Env env{*this, features};
        auto const feeDrops = env.current()->fees().base;
        auto const writer = Account("alice");
        auto const buyer = Account("bob");
        auto const gw = Account("gateway");
        auto const gme = Account("gme");
        auto const GME = gme["GME"];
        auto const USD = gw["USD"];

        env.fund(XRP(1'000'000), writer, buyer, gw, gme);
        env.close();
        env.trust(USD(1'000'000), writer, buyer);
        env.close();
        env(pay(gw, writer, USD(100'000)));
        env(pay(gw, buyer, USD(100'000)));
        env.close();
        env.trust(GME(100000), writer, buyer);
        env.close();
        env(pay(gme, writer, GME(10'000)));
        env.close();

        auto const preWriterXrp = env.balance(writer);
        auto const preWriterGme = env.balance(writer, GME);
        auto const preWriterUsd = env.balance(writer, USD);
        auto const preBuyerXrp = env.balance(buyer);
        auto const preBuyerGme = env.balance(buyer, GME);
        auto const preBuyerUsd = env.balance(buyer, USD);

        auto const expiration = env.now() + 80s;
        auto const strikePrice = USD(20);
        std::int64_t const strike =
            static_cast<std::int64_t>(Number(strikePrice.value()));
        uint256 const optionId{
            getOptionIndex(gme.id(), GME.currency, strike, expiration)};
        initPair(env, gme, GME.issue(), USD.issue());

        auto const premium = USD(0.5);
        auto const quantity = 1000;

        // create sell offer
        uint256 const sellId = createOffer(
            env,
            writer,
            env.seq(writer),
            GME,
            quantity,
            expiration,
            strikePrice.value(),
            premium.value(),
            tfSell);

        // check balances
        BEAST_EXPECT(env.balance(writer) == preWriterXrp - feeDrops);
        BEAST_EXPECT(env.balance(writer, GME) == preWriterGme - GME(quantity));
        BEAST_EXPECT(env.balance(writer, USD) == preWriterUsd);
        BEAST_EXPECT(env.balance(buyer) == preBuyerXrp);
        BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
        BEAST_EXPECT(env.balance(buyer, USD) == preBuyerUsd);

        // check meta data
        BEAST_EXPECT(inOwnerDir(*env.current(), writer, sellId));

        // create buy offer
        uint256 const buyId = createOffer(
            env,
            buyer,
            env.seq(buyer),
            GME,
            quantity,
            expiration,
            strikePrice.value(),
            premium.value(),
            0);

        // check balances
        BEAST_EXPECT(env.balance(writer) == preWriterXrp - feeDrops);
        BEAST_EXPECT(env.balance(writer, GME) == preWriterGme - GME(quantity));
        BEAST_EXPECT(env.balance(writer, USD) == preWriterUsd + USD(500));
        BEAST_EXPECT(env.balance(buyer) == preBuyerXrp - feeDrops);
        BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
        BEAST_EXPECT(env.balance(buyer, USD) == preBuyerUsd - USD(500));

        // check meta data
        BEAST_EXPECT(inOwnerDir(*env.current(), writer, sellId));
        BEAST_EXPECT(inOwnerDir(*env.current(), buyer, buyId));

        // exercise buy offer
        env(optionSettle(buyer, optionId, buyId),
            txflags(tfExercise),
            ter(tesSUCCESS));
        env.close();

        // check balances
        BEAST_EXPECT(env.balance(writer) == preWriterXrp - feeDrops);
        BEAST_EXPECT(env.balance(writer, GME) == preWriterGme - GME(quantity));
        BEAST_EXPECT(
            env.balance(writer, USD) == preWriterUsd + USD(500) + USD(20'000));
        BEAST_EXPECT(env.balance(buyer) == preBuyerXrp - (feeDrops * 2));
        BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme + GME(quantity));
        BEAST_EXPECT(
            env.balance(buyer, USD) == preBuyerUsd - USD(500) - USD(20'000));

        // check meta data
        BEAST_EXPECT(!inOwnerDir(*env.current(), writer, sellId));
        BEAST_EXPECT(!inOwnerDir(*env.current(), buyer, buyId));
    }

    void
    testExercisePut(FeatureBitset features)
    {
        using namespace test::jtx;
        using namespace std::literals::chrono_literals;
        testcase("exercise put");

        Env env{*this, features};
        auto const feeDrops = env.current()->fees().base;
        auto const writer = Account("alice");
        auto const buyer = Account("bob");
        auto const gw = Account("gateway");
        auto const gme = Account("gme");
        auto const GME = gme["GME"];
        auto const USD = gw["USD"];

        env.fund(XRP(1'000'000), writer, buyer, gw, gme);
        env.close();
        env.trust(USD(1'000'000), writer, buyer);
        env.close();
        env(pay(gw, writer, USD(100'000)));
        env(pay(gw, buyer, USD(100'000)));
        env.close();
        env.trust(GME(100000), writer, buyer);
        env.close();
        env(pay(gme, buyer, GME(10'000)));
        env.close();

        auto const preWriterXrp = env.balance(writer);
        auto const preWriterGme = env.balance(writer, GME);
        auto const preWriterUsd = env.balance(writer, USD);
        auto const preBuyerXrp = env.balance(buyer);
        auto const preBuyerGme = env.balance(buyer, GME);
        auto const preBuyerUsd = env.balance(buyer, USD);

        auto const expiration = env.now() + 80s;
        auto const strikePrice = USD(20);
        std::int64_t const strike =
            static_cast<std::int64_t>(Number(strikePrice.value()));
        uint256 const optionId{
            getOptionIndex(gme.id(), GME.currency, strike, expiration)};
        initPair(env, gme, GME.issue(), USD.issue());

        auto const premium = USD(0.5);
        auto const quantity = 1000;

        // create sell offer
        uint256 const sellId = createOffer(
            env,
            writer,
            env.seq(writer),
            GME,
            quantity,
            expiration,
            strikePrice.value(),
            premium.value(),
            tfSell | tfPut);

        // check balances
        BEAST_EXPECT(env.balance(writer) == preWriterXrp - feeDrops);
        BEAST_EXPECT(env.balance(writer, GME) == preWriterGme);
        BEAST_EXPECT(
            env.balance(writer, USD) == preWriterUsd - USD(strike * quantity));
        BEAST_EXPECT(env.balance(buyer) == preBuyerXrp);
        BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
        BEAST_EXPECT(env.balance(buyer, USD) == preBuyerUsd);

        // check meta data
        BEAST_EXPECT(inOwnerDir(*env.current(), writer, sellId));

        // create buy offer
        uint256 const buyId = createOffer(
            env,
            buyer,
            env.seq(buyer),
            GME,
            quantity,
            expiration,
            strikePrice.value(),
            premium.value(),
            tfPut);

        // check balances
        BEAST_EXPECT(env.balance(writer) == preWriterXrp - feeDrops);
        BEAST_EXPECT(env.balance(writer, GME) == preWriterGme);
        BEAST_EXPECT(
            env.balance(writer, USD) ==
            preWriterUsd - USD(strike * quantity) + USD(500));
        BEAST_EXPECT(env.balance(buyer) == preBuyerXrp - feeDrops);
        BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
        BEAST_EXPECT(env.balance(buyer, USD) == preBuyerUsd - USD(500));

        // check meta data
        BEAST_EXPECT(inOwnerDir(*env.current(), writer, sellId));
        BEAST_EXPECT(inOwnerDir(*env.current(), buyer, buyId));

        // exercise buy offer
        env(optionSettle(buyer, optionId, buyId),
            txflags(tfExercise),
            ter(tesSUCCESS));
        env.close();

        BEAST_EXPECT(env.balance(writer) == preWriterXrp - feeDrops);
        BEAST_EXPECT(env.balance(writer, GME) == preWriterGme + GME(quantity));
        BEAST_EXPECT(
            env.balance(writer, USD) ==
            preWriterUsd - USD(strike * quantity) + USD(500));
        BEAST_EXPECT(env.balance(buyer) == preBuyerXrp - (feeDrops * 2));
        BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme - GME(quantity));
        BEAST_EXPECT(
            env.balance(buyer, USD) == preBuyerUsd - USD(500) + USD(20'000));

        // check meta data
        BEAST_EXPECT(!inOwnerDir(*env.current(), writer, sellId));
        BEAST_EXPECT(!inOwnerDir(*env.current(), buyer, buyId));
    }

    void
    testExpireBuyCall(FeatureBitset features)
    {
        using namespace test::jtx;
        using namespace std::literals::chrono_literals;
        testcase("expire buy call");

        Env env{*this, features};
        auto const feeDrops = env.current()->fees().base;
        auto const buyer = Account("buyer");
        auto const gw = Account("gateway");
        auto const gme = Account("gme");
        auto const GME = gme["GME"];
        auto const USD = gw["USD"];

        env.fund(XRP(1'000'000), buyer, gw, gme);
        env.close();
        env.trust(USD(1'000'000), buyer);
        env.close();
        env(pay(gw, buyer, USD(100'000)));
        env.close();

        auto const preBuyerXrp = env.balance(buyer);
        auto const preBuyerGme = env.balance(buyer, GME);
        auto const preBuyerUsd = env.balance(buyer, USD);

        auto const expiration = env.now() + 10s;
        auto const strikePrice = USD(20);
        std::int64_t const strike =
            static_cast<std::int64_t>(Number(strikePrice.value()));
        uint256 const optionId{
            getOptionIndex(gme.id(), GME.currency, strike, expiration)};
        initPair(env, gme, GME.issue(), USD.issue());

        auto const premium = USD(0.5);
        auto const quantity = 1000;

        // create buy offer
        uint256 const buyId = createOffer(
            env,
            buyer,
            env.seq(buyer),
            GME,
            quantity,
            expiration,
            strikePrice.value(),
            premium.value(),
            0);

        // check balances
        BEAST_EXPECT(env.balance(buyer) == preBuyerXrp - feeDrops);
        BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
        BEAST_EXPECT(env.balance(buyer, USD) == preBuyerUsd);

        // check meta data
        BEAST_EXPECT(inOwnerDir(*env.current(), buyer, buyId));

        // expire buy offer
        env(optionSettle(buyer, optionId, buyId),
            txflags(tfExpire),
            ter(tecEXPIRED));
        env.close();

        BEAST_EXPECT(env.balance(buyer) == preBuyerXrp - (feeDrops * 2));
        BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
        BEAST_EXPECT(env.balance(buyer, USD) == preBuyerUsd);

        // check meta data
        BEAST_EXPECT(!inOwnerDir(*env.current(), buyer, buyId));
        auto const jrr =
            getOptionBookOffers(env, GME.issue(), strikePrice, expiration);
        BEAST_EXPECT(jrr[jss::offers].size() == 0);
    }

    void
    testExpireBuyPut(FeatureBitset features)
    {
        using namespace test::jtx;
        using namespace std::literals::chrono_literals;
        testcase("expire buy put");

        Env env{*this, features};
        auto const feeDrops = env.current()->fees().base;
        auto const buyer = Account("buyer");
        auto const gw = Account("gateway");
        auto const gme = Account("gme");
        auto const GME = gme["GME"];
        auto const USD = gw["USD"];

        env.fund(XRP(1'000'000), buyer, gw, gme);
        env.close();
        env.trust(USD(1'000'000), buyer);
        env.close();
        env(pay(gw, buyer, USD(100'000)));
        env.close();

        auto const preBuyerXrp = env.balance(buyer);
        auto const preBuyerGme = env.balance(buyer, GME);
        auto const preBuyerUsd = env.balance(buyer, USD);

        auto const expiration = env.now() + 10s;
        auto const strikePrice = USD(20);
        std::int64_t const strike =
            static_cast<std::int64_t>(Number(strikePrice.value()));
        uint256 const optionId{
            getOptionIndex(gme.id(), GME.currency, strike, expiration)};
        initPair(env, gme, GME.issue(), USD.issue());

        auto const premium = USD(0.5);
        auto const quantity = 1000;

        // create buy offer
        uint256 const buyId = createOffer(
            env,
            buyer,
            env.seq(buyer),
            GME,
            quantity,
            expiration,
            strikePrice.value(),
            premium.value(),
            tfPut);

        // check balances
        BEAST_EXPECT(env.balance(buyer) == preBuyerXrp - feeDrops);
        BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
        BEAST_EXPECT(env.balance(buyer, USD) == preBuyerUsd);

        // check meta data
        BEAST_EXPECT(inOwnerDir(*env.current(), buyer, buyId));

        // expire buy offer
        env(optionSettle(buyer, optionId, buyId),
            txflags(tfExpire),
            ter(tecEXPIRED));
        env.close();

        BEAST_EXPECT(env.balance(buyer) == preBuyerXrp - (feeDrops * 2));
        BEAST_EXPECT(env.balance(buyer, GME) == preBuyerGme);
        BEAST_EXPECT(env.balance(buyer, USD) == preBuyerUsd);

        // check meta data
        BEAST_EXPECT(!inOwnerDir(*env.current(), buyer, buyId));
        auto const jrr =
            getOptionBookOffers(env, GME.issue(), strikePrice, expiration);
        BEAST_EXPECT(jrr[jss::offers].size() == 0);
    }

    void
    testExpireSellCall(FeatureBitset features)
    {
        using namespace test::jtx;
        using namespace std::literals::chrono_literals;
        testcase("expire sell call");

        Env env{*this, features};
        auto const feeDrops = env.current()->fees().base;
        auto const writer = Account("writer");
        auto const gw = Account("gateway");
        auto const gme = Account("gme");
        auto const GME = gme["GME"];
        auto const USD = gw["USD"];

        env.fund(XRP(1'000'000), writer, gw, gme);
        env.close();
        env.trust(USD(1'000'000), writer);
        env.close();
        env(pay(gw, writer, USD(100'000)));
        env.close();
        env.trust(GME(100000), writer);
        env.close();
        env(pay(gme, writer, GME(10'000)));
        env.close();

        auto const preWriterXrp = env.balance(writer);
        auto const preWriterGme = env.balance(writer, GME);
        auto const preWriterUsd = env.balance(writer, USD);

        auto const expiration = env.now() + 10s;
        auto const strikePrice = USD(20);
        std::int64_t const strike =
            static_cast<std::int64_t>(Number(strikePrice.value()));
        uint256 const optionId{
            getOptionIndex(gme.id(), GME.currency, strike, expiration)};
        initPair(env, gme, GME.issue(), USD.issue());

        auto const premium = USD(0.5);
        auto const quantity = 1000;

        // create sell offer
        uint256 const sellId = createOffer(
            env,
            writer,
            env.seq(writer),
            GME,
            quantity,
            expiration,
            strikePrice.value(),
            premium.value(),
            tfSell);

        // check balances
        BEAST_EXPECT(env.balance(writer) == preWriterXrp - feeDrops);
        BEAST_EXPECT(env.balance(writer, GME) == preWriterGme - GME(quantity));
        BEAST_EXPECT(env.balance(writer, USD) == preWriterUsd);

        // check meta data
        BEAST_EXPECT(inOwnerDir(*env.current(), writer, sellId));

        // expire sell offer
        env(optionSettle(writer, optionId, sellId),
            txflags(tfExpire),
            ter(tecEXPIRED));
        env.close();

        BEAST_EXPECT(env.balance(writer) == preWriterXrp - (feeDrops * 2));
        BEAST_EXPECT(env.balance(writer, GME) == preWriterGme);
        BEAST_EXPECT(env.balance(writer, USD) == preWriterUsd);

        // check meta data
        BEAST_EXPECT(!inOwnerDir(*env.current(), writer, sellId));
        auto const jrr =
            getOptionBookOffers(env, GME.issue(), strikePrice, expiration);
        BEAST_EXPECT(jrr[jss::offers].size() == 0);
    }

    void
    testExpireSellPut(FeatureBitset features)
    {
        using namespace test::jtx;
        using namespace std::literals::chrono_literals;
        testcase("expire sell put");

        Env env{*this, features};
        auto const feeDrops = env.current()->fees().base;
        auto const writer = Account("writer");
        auto const gw = Account("gateway");
        auto const gme = Account("gme");
        auto const GME = gme["GME"];
        auto const USD = gw["USD"];

        env.fund(XRP(1'000'000), writer, gw, gme);
        env.close();
        env.trust(USD(1'000'000), writer);
        env.close();
        env(pay(gw, writer, USD(100'000)));
        env.close();

        auto const preWriterXrp = env.balance(writer);
        auto const preWriterGme = env.balance(writer, GME);
        auto const preWriterUsd = env.balance(writer, USD);

        auto const expiration = env.now() + 10s;
        auto const strikePrice = USD(20);
        std::int64_t const strike =
            static_cast<std::int64_t>(Number(strikePrice.value()));
        uint256 const optionId{
            getOptionIndex(gme.id(), GME.currency, strike, expiration)};
        initPair(env, gme, GME.issue(), USD.issue());

        auto const premium = USD(0.5);
        auto const quantity = 1000;

        // create sell offer
        uint256 const sellId = createOffer(
            env,
            writer,
            env.seq(writer),
            GME,
            quantity,
            expiration,
            strikePrice.value(),
            premium.value(),
            tfSell | tfPut);

        // check balances
        BEAST_EXPECT(env.balance(writer) == preWriterXrp - feeDrops);
        BEAST_EXPECT(env.balance(writer, GME) == preWriterGme);
        BEAST_EXPECT(
            env.balance(writer, USD) == preWriterUsd - USD(strike * quantity));

        // check meta data
        BEAST_EXPECT(inOwnerDir(*env.current(), writer, sellId));

        // expire sell offer
        env(optionSettle(writer, optionId, sellId),
            txflags(tfExpire),
            ter(tecEXPIRED));
        env.close();

        BEAST_EXPECT(env.balance(writer) == preWriterXrp - (feeDrops * 2));
        BEAST_EXPECT(env.balance(writer, GME) == preWriterGme);
        BEAST_EXPECT(env.balance(writer, USD) == preWriterUsd);

        // check meta data
        BEAST_EXPECT(!inOwnerDir(*env.current(), writer, sellId));
        auto const jrr =
            getOptionBookOffers(env, GME.issue(), strikePrice, expiration);
        BEAST_EXPECT(jrr[jss::offers].size() == 0);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        auto const sa = supported_amendments();
        testEnabled(sa);
        testSettleInvalid(sa);
        testCreateBuyValid(sa);
        testCreateSellValid(sa);
        testCloseBuyCall(sa);
        testCloseBuyPut(sa);
        testCloseSellCall(sa);
        testCloseSellPut(sa);
        testExerciseCall(sa);
        testExercisePut(sa);
        testExpireBuyCall(sa);
        testExpireBuyPut(sa);
        testExpireSellCall(sa);
        testExpireSellPut(sa);
    }
};

BEAST_DEFINE_TESTSUITE(Option, app, ripple);

}  // namespace test
}  // namespace ripple