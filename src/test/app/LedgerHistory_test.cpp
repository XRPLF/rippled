//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.

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

#include <ripple/app/ledger/LedgerHistory.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/tx/apply.h>
#include <ripple/beast/insight/NullCollector.h>
#include <ripple/beast/unit_test.h>
#include <ripple/ledger/OpenView.h>
#include <chrono>
#include <memory>
#include <sstream>
#include <test/jtx.h>

namespace ripple {
namespace test {

class LedgerHistory_test : public beast::unit_test::suite
{
public:
    /** Log manager that searches for a specific message substring
     */
    class CheckMessageLogs : public Logs
    {
        std::string msg_;
        bool& found_;

        class CheckMessageSink : public beast::Journal::Sink
        {
            CheckMessageLogs& owner_;

        public:
            CheckMessageSink(
                beast::severities::Severity threshold,
                CheckMessageLogs& owner)
                : beast::Journal::Sink(threshold, false), owner_(owner)
            {
            }

            void
            write(beast::severities::Severity level, std::string const& text)
                override
            {
                if (text.find(owner_.msg_) != std::string::npos)
                    owner_.found_ = true;
            }
        };

    public:
        /** Constructor

            @param msg The message string to search for
            @param found The variable to set to true if the message is found
        */
        CheckMessageLogs(std::string msg, bool& found)
            : Logs{beast::severities::kDebug}
            , msg_{std::move(msg)}
            , found_{found}
        {
        }

        std::unique_ptr<beast::Journal::Sink>
        makeSink(
            std::string const& partition,
            beast::severities::Severity threshold) override
        {
            return std::make_unique<CheckMessageSink>(threshold, *this);
        }
    };

    /** Generate a new ledger by hand, applying a specific close time offset
        and optionally inserting a transaction.

        If prev is nullptr, then the genesis ledger is made and no offset or
        transaction is applied.

    */
    static std::shared_ptr<Ledger>
    makeLedger(
        std::shared_ptr<Ledger const> const& prev,
        jtx::Env& env,
        LedgerHistory& lh,
        NetClock::duration closeOffset,
        std::shared_ptr<STTx const> stx = {})
    {
        if (!prev)
        {
            assert(!stx);
            return std::make_shared<Ledger>(
                create_genesis,
                env.app().config(),
                std::vector<uint256>{},
                env.app().family());
        }
        auto res = std::make_shared<Ledger>(
            *prev, prev->info().closeTime + closeOffset);

        if (stx)
        {
            OpenView accum(&*res);
            applyTransaction(
                env.app(), accum, *stx, false, tapNO_CHECK_SIGN, env.journal);
            accum.apply(*res);
        }
        res->updateSkipList();

        {
            res->stateMap().flushDirty(hotACCOUNT_NODE, res->info().seq);
            res->txMap().flushDirty(hotTRANSACTION_NODE, res->info().seq);
        }
        res->unshare();

        // Accept ledger
        res->setAccepted(
            res->info().closeTime,
            res->info().closeTimeResolution,
            true /* close time correct*/,
            env.app().config());
        lh.insert(res, false);
        return res;
    }

    void
    testHandleMismatch()
    {
        testcase("LedgerHistory mismatch");
        using namespace jtx;
        using namespace std::chrono;

        // No mismatch
        {
            bool found = false;
            Env env{*this,
                    envconfig(),
                    std::make_unique<CheckMessageLogs>("MISMATCH ", found)};
            LedgerHistory lh{beast::insight::NullCollector::New(), env.app()};
            auto const genesis = makeLedger({}, env, lh, 0s);
            uint256 const dummyTxHash{1};
            lh.builtLedger(genesis, dummyTxHash, {});
            lh.validatedLedger(genesis, dummyTxHash);

            BEAST_EXPECT(!found);
        }

        // Close time mismatch
        {
            bool found = false;
            Env env{*this,
                    envconfig(),
                    std::make_unique<CheckMessageLogs>(
                        "MISMATCH on close time", found)};
            LedgerHistory lh{beast::insight::NullCollector::New(), env.app()};
            auto const genesis = makeLedger({}, env, lh, 0s);
            auto const ledgerA = makeLedger(genesis, env, lh, 4s);
            auto const ledgerB = makeLedger(genesis, env, lh, 40s);

            uint256 const dummyTxHash{1};
            lh.builtLedger(ledgerA, dummyTxHash, {});
            lh.validatedLedger(ledgerB, dummyTxHash);

            BEAST_EXPECT(found);
        }

        // Prior ledger mismatch
        {
            bool found = false;
            Env env{*this,
                    envconfig(),
                    std::make_unique<CheckMessageLogs>(
                        "MISMATCH on prior ledger", found)};
            LedgerHistory lh{beast::insight::NullCollector::New(), env.app()};
            auto const genesis = makeLedger({}, env, lh, 0s);
            auto const ledgerA = makeLedger(genesis, env, lh, 4s);
            auto const ledgerB = makeLedger(genesis, env, lh, 40s);
            auto const ledgerAC = makeLedger(ledgerA, env, lh, 4s);
            auto const ledgerBD = makeLedger(ledgerB, env, lh, 4s);

            uint256 const dummyTxHash{1};
            lh.builtLedger(ledgerAC, dummyTxHash, {});
            lh.validatedLedger(ledgerBD, dummyTxHash);

            BEAST_EXPECT(found);
        }

        // Simulate a bug in which consensus may agree on transactions, but
        // somehow generate different ledgers
        for (bool const txBug : {true, false})
        {
            std::string const msg = txBug
                ? "MISMATCH with same consensus transaction set"
                : "MISMATCH on consensus transaction set";
            bool found = false;
            Env env{*this,
                    envconfig(),
                    std::make_unique<CheckMessageLogs>(msg, found)};
            LedgerHistory lh{beast::insight::NullCollector::New(), env.app()};

            Account alice{"A1"};
            Account bob{"A2"};
            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const ledgerBase =
                env.app().getLedgerMaster().getClosedLedger();

            JTx txAlice = env.jt(noop(alice));
            auto const ledgerA =
                makeLedger(ledgerBase, env, lh, 4s, txAlice.stx);

            JTx txBob = env.jt(noop(bob));
            auto const ledgerB = makeLedger(ledgerBase, env, lh, 4s, txBob.stx);

            lh.builtLedger(ledgerA, txAlice.stx->getTransactionID(), {});
            // Simulate the bug by claiming ledgerB had the same consensus hash
            // as ledgerA, but somehow generated different ledgers
            lh.validatedLedger(
                ledgerB,
                txBug ? txAlice.stx->getTransactionID()
                      : txBob.stx->getTransactionID());

            BEAST_EXPECT(found);
        }
    }

    void
    run()
    {
        testHandleMismatch();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerHistory, app, ripple);

}  // namespace test
}  // namespace ripple
