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

#include <BeastConfig.h>
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

        auto tr = ac.checkInvariants(tesSUCCESS);
        if (enabled)
        {
            BEAST_EXPECT(tr == tecINVARIANT_FAILED);
            BEAST_EXPECT(boost::starts_with(sink.strm_.str(), "Invariant failed:"));
            //uncomment if you want to log the invariant failure message
            //log << "   --> " << sink.strm_.str() << std::endl;
        }
        else
        {
            BEAST_EXPECT(tr == tesSUCCESS);
            BEAST_EXPECT(sink.strm_.str().empty());
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

public:
    void run ()
    {
        testEnabled ();
        // all invariant checks are run with
        // the checks enabled and disabled
        for(auto const& b : {true, false})
        {
            testXRPNotCreated (b);
            testAccountsNotRemoved (b);
            testTypesMatch (b);
            testNoXRPTrustLine (b);
        }
    }
};

BEAST_DEFINE_TESTSUITE (Invariants, ledger, ripple);

}  // ripple
