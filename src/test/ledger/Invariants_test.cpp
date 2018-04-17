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

    class TestSink : public beast::Journal::Sink
    {
    public:
        std::stringstream strm_;

        TestSink () : Sink (beast::severities::kWarning, false) {  }

        void
        write (beast::severities::Severity level,
            std::string const& text) override
        {
            if (level < threshold())
                return;

            strm_ << text << std::endl;
        }
    };

    // this is common setup/method for running a failing invariant check. The
    // precheck function is used to manipulate the ApplyContext with view
    // changes that will cause the check to fail.
    void
    doInvariantCheck( bool enabled,
        std::vector<std::string> const& expect_logs,
        std::function <
            bool (
                test::jtx::Account const& a,
                test::jtx::Account const& b,
                ApplyContext& ac)>
            const& precheck )
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

        // dummy/empty tx to setup the AccountContext
        auto tx = STTx {ttACCOUNT_SET, [](STObject&){  } };
        OpenView ov {*env.current()};
        TestSink sink;
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

        auto tr = tesSUCCESS;
        // invoke check twice to cover tec and tef cases
        for (auto i : {0,1})
        {
            tr = ac.checkInvariants(tr, XRPAmount{});
            if (enabled)
            {
                BEAST_EXPECT(
                    tr == (i == 0 ? tecINVARIANT_FAILED : tefINVARIANT_FAILED));
                BEAST_EXPECT(
                    boost::starts_with(sink.strm_.str(), "Invariant failed:") ||
                    boost::starts_with(sink.strm_.str(),
                        "Transaction caused an exception"));
                //uncomment if you want to log the invariant failure message
                //log << "   --> " << sink.strm_.str() << std::endl;
                for (auto const& m : expect_logs)
                {
                    BEAST_EXPECT(sink.strm_.str().find(m) != std::string::npos);
                }
            }
            else
            {
                BEAST_EXPECT(tr == tesSUCCESS);
                BEAST_EXPECT(sink.strm_.str().empty());
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
    testAccountsNotRemoved(bool enabled)
    {
        using namespace test::jtx;
        testcase << "checks " << (enabled ? "enabled" : "disabled") <<
            " - account root removed";
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

public:
    void run ()
    {
        testEnabled ();

        // now run each invariant check test with
        // the feature enabled and disabled
        for(auto const& b : {false, true})
        {
            testXRPNotCreated (b);
            testAccountsNotRemoved (b);
            testTypesMatch (b);
            testNoXRPTrustLine (b);
            testXRPBalanceCheck (b);
            testNoBadOffers (b);
            testNoZeroEscrow (b);
        }
    }
};

BEAST_DEFINE_TESTSUITE (Invariants, ledger, ripple);

}  // ripple

