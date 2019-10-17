//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc.

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
#include <test/jtx/Env.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/app/tx/apply.h>
#include <ripple/app/tx/impl/Transactor.h>
#include <ripple/app/tx/impl/ApplyContext.h>
#include <ripple/protocol/STLedgerEntry.h>
#include <boost/algorithm/string/predicate.hpp>

namespace ripple {

class Invariants_test : public beast::unit_test::suite
{
    // this is common setup/method for running a failing invariant check. The
    // precheck function is used to manipulate the ApplyContext with view
    // changes that will cause the check to fail.
    using Precheck = std::function <
        bool (
            test::jtx::Account const& a,
            test::jtx::Account const& b,
            ApplyContext& ac)>;

    void
    doInvariantCheck( bool enabled,
        std::vector<std::string> const& expect_logs,
        Precheck const& precheck,
        XRPAmount fee = XRPAmount{},
        STTx tx = STTx {ttACCOUNT_SET, [](STObject&){  }},
        std::initializer_list<TER> ters =
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED})
    {
        using namespace test::jtx;
        Env env {*this};
        if (! enabled)
        {
            auto& features = env.app().config().features;
            auto it = features.find(featureEnforceInvariants);
            if (it != features.end())
                features.erase(it);
        }

        Account A1 {"A1"};
        Account A2 {"A2"};
        env.fund (XRP (1000), A1, A2);
        env.close();

        OpenView ov {*env.current()};
        test::StreamSink sink {beast::severities::kWarning};
        beast::Journal jlog {sink};
        ApplyContext ac {
            env.app(),
            ov,
            tx,
            tesSUCCESS,
            env.current()->fees().base,
            tapNONE,
            jlog
        };

        BEAST_EXPECT(precheck(A1, A2, ac));

        // invoke check twice to cover tec and tef cases
        if (! BEAST_EXPECT(ters.size() == 2))
            return;

        TER terActual = tesSUCCESS;
        for (TER const terExpect : ters)
        {
            terActual = ac.checkInvariants(terActual, fee);
            if (enabled)
            {
                BEAST_EXPECT(terExpect == terActual);
                BEAST_EXPECT(
                    boost::starts_with(sink.messages().str(), "Invariant failed:") ||
                    boost::starts_with(sink.messages().str(),
                        "Transaction caused an exception"));
                //uncomment if you want to log the invariant failure message
                //log << "   --> " << sink.messages().str() << std::endl;
                for (auto const& m : expect_logs)
                {
                    BEAST_EXPECT(sink.messages().str().find(m) != std::string::npos);
                }
            }
            else
            {
                BEAST_EXPECT(terActual == tesSUCCESS);
                BEAST_EXPECT(sink.messages().str().empty());
            }
        }
    }

    void
    testEnabled ()
    {
        using namespace test::jtx;
        testcase ("feature enabled");
        Env env {*this};

        auto hasInvariants =
            env.app().config().features.find (featureEnforceInvariants);
        BEAST_EXPECT(hasInvariants != env.app().config().features.end());

        BEAST_EXPECT(env.current()->rules().enabled(featureEnforceInvariants));
    }

    void
    testXRPNotCreated (bool enabled)
    {
        using namespace test::jtx;
        testcase << "checks " << (enabled ? "enabled" : "disabled") <<
            " - XRP created";
        doInvariantCheck (enabled,
            {{ "XRP net change was positive: 500" }},
            [](Account const& A1, Account const&, ApplyContext& ac)
            {
                // put a single account in the view and "manufacture" some XRP
                auto const sle = ac.view().peek (keylet::account(A1.id()));
                if(! sle)
                    return false;
                auto amt = sle->getFieldAmount (sfBalance);
                sle->setFieldAmount (sfBalance, amt + 500);
                ac.view().update (sle);
                return true;
            });
    }

    void
    testAccountRootsNotRemoved(bool enabled)
    {
        using namespace test::jtx;
        testcase << "checks " << (enabled ? "enabled" : "disabled") <<
            " - account root removed";

        // An account was deleted, but not by an AccountDelete transaction.
        doInvariantCheck (enabled,
            {{ "an account root was deleted" }},
            [](Account const& A1, Account const&, ApplyContext& ac)
            {
                // remove an account from the view
                auto const sle = ac.view().peek (keylet::account(A1.id()));
                if(! sle)
                    return false;
                ac.view().erase (sle);
                return true;
            });

        // Successful AccountDelete transaction that didn't delete an account.
        //
        // Note that this is a case where a second invocation of the invariant
        // checker returns a tecINVARIANT_FAILED, not a tefINVARIANT_FAILED.
        // After a discussion with the team, we believe that's okay.
        doInvariantCheck (enabled,
            {{ "account deletion succeeded without deleting an account" }},
            [](Account const&, Account const&, ApplyContext& ac){return true;},
            XRPAmount{},
            STTx {ttACCOUNT_DELETE, [](STObject& tx){ }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});

        // Successful AccountDelete that deleted more than one account.
        doInvariantCheck (enabled,
            {{ "account deletion succeeded but deleted multiple accounts" }},
            [](Account const& A1, Account const& A2, ApplyContext& ac)
            {
                // remove two accounts from the view
                auto const sleA1 = ac.view().peek (keylet::account(A1.id()));
                auto const sleA2 = ac.view().peek (keylet::account(A2.id()));
                if(!sleA1 || !sleA2)
                    return false;
                ac.view().erase (sleA1);
                ac.view().erase (sleA2);
                return true;
            },
            XRPAmount{},
            STTx {ttACCOUNT_DELETE, [](STObject& tx){ }});
    }

    void
    testTypesMatch(bool enabled)
    {
        using namespace test::jtx;
        testcase << "checks " << (enabled ? "enabled" : "disabled") <<
            " - LE types don't match";
        doInvariantCheck (enabled,
            {{ "ledger entry type mismatch" },
             { "XRP net change of -1000000000 doesn't match fee 0" }},
            [](Account const& A1, Account const&, ApplyContext& ac)
            {
                // replace an entry in the table with an SLE of a different type
                auto const sle = ac.view().peek (keylet::account(A1.id()));
                if(! sle)
                    return false;
                auto sleNew = std::make_shared<SLE> (ltTICKET, sle->key());
                ac.rawView().rawReplace (sleNew);
                return true;
            });

        doInvariantCheck (enabled,
            {{ "invalid ledger entry type added" }},
            [](Account const& A1, Account const&, ApplyContext& ac)
            {
                // add an entry in the table with an SLE of an invalid type
                auto const sle = ac.view().peek (keylet::account(A1.id()));
                if(! sle)
                    return false;
                // make a dummy escrow ledger entry, then change the type to an
                // unsupported value so that the valid type invariant check
                // will fail.
                auto sleNew = std::make_shared<SLE> (
                    keylet::escrow(A1, (*sle)[sfSequence] + 2));
                sleNew->type_ = ltNICKNAME;
                ac.view().insert (sleNew);
                return true;
            });
    }

    void
    testNoXRPTrustLine(bool enabled)
    {
        using namespace test::jtx;
        testcase << "checks " << (enabled ? "enabled" : "disabled") <<
            " - trust lines with XRP not allowed";
        doInvariantCheck (enabled,
            {{ "an XRP trust line was created" }},
            [](Account const& A1, Account const& A2, ApplyContext& ac)
            {
                // create simple trust SLE with xrp currency
                auto index = getRippleStateIndex (A1, A2, xrpIssue().currency);
                auto const sleNew = std::make_shared<SLE>(
                    ltRIPPLE_STATE, index);
                ac.view().insert (sleNew);
                return true;
            });
    }

    void
    testXRPBalanceCheck(bool enabled)
    {
        using namespace test::jtx;
        testcase << "checks " << (enabled ? "enabled" : "disabled") <<
            " - XRP balance checks";

        doInvariantCheck (enabled,
            {{ "Cannot return non-native STAmount as XRPAmount" }},
            [](Account const& A1, Account const& A2, ApplyContext& ac)
            {
                //non-native balance
                auto const sle = ac.view().peek (keylet::account(A1.id()));
                if(! sle)
                    return false;
                STAmount nonNative (A2["USD"](51));
                sle->setFieldAmount (sfBalance, nonNative);
                ac.view().update (sle);
                return true;
            });

        doInvariantCheck (enabled,
            {{ "incorrect account XRP balance" },
             {  "XRP net change was positive: 99999999000000001" }},
            [](Account const& A1, Account const&, ApplyContext& ac)
            {
                // balance exceeds genesis amount
                auto const sle = ac.view().peek (keylet::account(A1.id()));
                if(! sle)
                    return false;
                sle->setFieldAmount (sfBalance, SYSTEM_CURRENCY_START + 1);
                ac.view().update (sle);
                return true;
            });

        doInvariantCheck (enabled,
            {{ "incorrect account XRP balance" },
             { "XRP net change of -1000000001 doesn't match fee 0" }},
            [](Account const& A1, Account const&, ApplyContext& ac)
            {
                // balance is negative
                auto const sle = ac.view().peek (keylet::account(A1.id()));
                if(! sle)
                    return false;
                sle->setFieldAmount (sfBalance, -1);
                ac.view().update (sle);
                return true;
            });
    }

    void
    testTransactionFeeCheck(bool enabled)
    {
        using namespace test::jtx;
        using namespace std::string_literals;
        testcase << "checks " << (enabled ? "enabled" : "disabled") <<
            " - Transaction fee checks";

        doInvariantCheck (enabled,
            {{ "fee paid was negative: -1" },
             { "XRP net change of 0 doesn't match fee -1" }},
            [](Account const&, Account const&, ApplyContext&) { return true; },
            XRPAmount{-1});

        doInvariantCheck (enabled,
            {{ "fee paid exceeds system limit: "s +
                std::to_string(SYSTEM_CURRENCY_START) },
             { "XRP net change of 0 doesn't match fee "s +
                std::to_string(SYSTEM_CURRENCY_START) }},
            [](Account const&, Account const&, ApplyContext&) { return true; },
            XRPAmount{SYSTEM_CURRENCY_START});

         doInvariantCheck (enabled,
            {{ "fee paid is 20 exceeds fee specified in transaction." },
             { "XRP net change of 0 doesn't match fee 20" }},
            [](Account const&, Account const&, ApplyContext&) { return true; },
            XRPAmount{20},
            STTx { ttACCOUNT_SET,
                [](STObject& tx){tx.setFieldAmount(sfFee, XRPAmount{10});} });
    }


    void
    testNoBadOffers(bool enabled)
    {
        using namespace test::jtx;
        testcase << "checks " << (enabled ? "enabled" : "disabled") <<
            " - no bad offers";

        doInvariantCheck (enabled,
            {{ "offer with a bad amount" }},
            [](Account const& A1, Account const&, ApplyContext& ac)
            {
                // offer with negative takerpays
                auto const sle = ac.view().peek (keylet::account(A1.id()));
                if(! sle)
                    return false;
                auto const offer_index =
                    getOfferIndex (A1.id(), (*sle)[sfSequence]);
                auto sleNew = std::make_shared<SLE> (ltOFFER, offer_index);
                sleNew->setAccountID (sfAccount, A1.id());
                sleNew->setFieldU32 (sfSequence, (*sle)[sfSequence]);
                sleNew->setFieldAmount (sfTakerPays, XRP(-1));
                ac.view().insert (sleNew);
                return true;
            });

        doInvariantCheck (enabled,
            {{ "offer with a bad amount" }},
            [](Account const& A1, Account const&, ApplyContext& ac)
            {
                // offer with negative takergets
                auto const sle = ac.view().peek (keylet::account(A1.id()));
                if(! sle)
                    return false;
                auto const offer_index =
                    getOfferIndex (A1.id(), (*sle)[sfSequence]);
                auto sleNew = std::make_shared<SLE> (ltOFFER, offer_index);
                sleNew->setAccountID (sfAccount, A1.id());
                sleNew->setFieldU32 (sfSequence, (*sle)[sfSequence]);
                sleNew->setFieldAmount (sfTakerPays, A1["USD"](10));
                sleNew->setFieldAmount (sfTakerGets, XRP(-1));
                ac.view().insert (sleNew);
                return true;
            });

        doInvariantCheck (enabled,
            {{ "offer with a bad amount" }},
            [](Account const& A1, Account const&, ApplyContext& ac)
            {
                // offer XRP to XRP
                auto const sle = ac.view().peek (keylet::account(A1.id()));
                if(! sle)
                    return false;
                auto const offer_index =
                    getOfferIndex (A1.id(), (*sle)[sfSequence]);
                auto sleNew = std::make_shared<SLE> (ltOFFER, offer_index);
                sleNew->setAccountID (sfAccount, A1.id());
                sleNew->setFieldU32 (sfSequence, (*sle)[sfSequence]);
                sleNew->setFieldAmount (sfTakerPays, XRP(10));
                sleNew->setFieldAmount (sfTakerGets, XRP(11));
                ac.view().insert (sleNew);
                return true;
            });
    }

    void
    testNoZeroEscrow(bool enabled)
    {
        using namespace test::jtx;
        testcase << "checks " << (enabled ? "enabled" : "disabled") <<
            " - no zero escrow";

        doInvariantCheck (enabled,
            {{ "Cannot return non-native STAmount as XRPAmount" }},
            [](Account const& A1, Account const& A2, ApplyContext& ac)
            {
                // escrow with nonnative amount
                auto const sle = ac.view().peek (keylet::account(A1.id()));
                if(! sle)
                    return false;
                auto sleNew = std::make_shared<SLE> (
                    keylet::escrow(A1, (*sle)[sfSequence] + 2));
                STAmount nonNative (A2["USD"](51));
                sleNew->setFieldAmount (sfAmount, nonNative);
                ac.view().insert (sleNew);
                return true;
            });

        doInvariantCheck (enabled,
            {{ "XRP net change of -1000000 doesn't match fee 0"},
             {  "escrow specifies invalid amount" }},
            [](Account const& A1, Account const&, ApplyContext& ac)
            {
                // escrow with negative amount
                auto const sle = ac.view().peek (keylet::account(A1.id()));
                if(! sle)
                    return false;
                auto sleNew = std::make_shared<SLE> (
                    keylet::escrow(A1, (*sle)[sfSequence] + 2));
                sleNew->setFieldAmount (sfAmount, XRP(-1));
                ac.view().insert (sleNew);
                return true;
            });

        doInvariantCheck (enabled,
            {{ "XRP net change was positive: 100000000000000001" },
             {  "escrow specifies invalid amount" }},
            [](Account const& A1, Account const&, ApplyContext& ac)
            {
                // escrow with too-large amount
                auto const sle = ac.view().peek (keylet::account(A1.id()));
                if(! sle)
                    return false;
                auto sleNew = std::make_shared<SLE> (
                    keylet::escrow(A1, (*sle)[sfSequence] + 2));
                sleNew->setFieldAmount (sfAmount, SYSTEM_CURRENCY_START + 1);
                ac.view().insert (sleNew);
                return true;
            });
    }

    void
    testValidNewAccountRoot(bool enabled)
    {
        using namespace test::jtx;
        testcase << "checks " << (enabled ? "enabled" : "disabled") <<
            " - valid new account root";

        doInvariantCheck (enabled,
            {{ "account root created by a non-Payment" }},
            [](Account const&, Account const&, ApplyContext& ac)
            {
                // Insert a new account root created by a non-payment into
                // the view.
                const Account A3 {"A3"};
                Keylet const acctKeylet = keylet::account (A3);
                auto const sleNew = std::make_shared<SLE>(acctKeylet);
                ac.view().insert (sleNew);
                return true;
            });

        doInvariantCheck (enabled,
            {{ "multiple accounts created in a single transaction" }},
            [](Account const&, Account const&, ApplyContext& ac)
            {
                // Insert two new account roots into the view.
                {
                    const Account A3 {"A3"};
                    Keylet const acctKeylet = keylet::account (A3);
                    auto const sleA3 = std::make_shared<SLE>(acctKeylet);
                    ac.view().insert (sleA3);
                }
                {
                    const Account A4 {"A4"};
                    Keylet const acctKeylet = keylet::account (A4);
                    auto const sleA4 = std::make_shared<SLE>(acctKeylet);
                    ac.view().insert (sleA4);
                }
                return true;
            });

        doInvariantCheck (enabled,
            {{ "account created with wrong starting sequence number" }},
            [](Account const&, Account const&, ApplyContext& ac)
            {
                // Insert a new account root with the wrong starting sequence.
                const Account A3 {"A3"};
                Keylet const acctKeylet = keylet::account (A3);
                auto const sleNew = std::make_shared<SLE>(acctKeylet);
                sleNew->setFieldU32 (sfSequence, ac.view().seq() + 1);
                ac.view().insert (sleNew);
                return true;
            },
            XRPAmount{},
            STTx {ttPAYMENT, [](STObject& tx){ }});
    }

public:
    void run () override
    {
        testEnabled ();

        // now run each invariant check test with
        // the feature enabled and disabled
        for(auto const& b : {false, true})
        {
            testXRPNotCreated (b);
            testAccountRootsNotRemoved (b);
            testTypesMatch (b);
            testNoXRPTrustLine (b);
            testXRPBalanceCheck (b);
            testTransactionFeeCheck(b);
            testNoBadOffers (b);
            testNoZeroEscrow (b);
            testValidNewAccountRoot (b);
        }
    }
};

BEAST_DEFINE_TESTSUITE (Invariants, ledger, ripple);

}  // ripple

