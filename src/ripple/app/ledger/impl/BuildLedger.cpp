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

#include <ripple/app/ledger/BuildLedger.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/ledger/LedgerReplay.h>
#include <ripple/app/ledger/OpenLedger.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/CanonicalTXSet.h>
#include <ripple/app/tx/apply.h>
#include <ripple/protocol/Feature.h>

namespace ripple {

/* Generic buildLedgerImpl that dispatches to ApplyTxs invocable with signature
    void(OpenView&, std::shared_ptr<Ledger> const&)
   It is responsible for adding transactions to the open view to generate the
   new ledger. It is generic since the mechanics differ for consensus
   generated ledgers versus replayed ledgers.
*/
template <class ApplyTxs>
std::shared_ptr<Ledger>
buildLedgerImpl(
    std::shared_ptr<Ledger const> const& parent,
    NetClock::time_point closeTime,
    const bool closeTimeCorrect,
    NetClock::duration closeResolution,
    Application& app,
    beast::Journal j,
    ApplyTxs&& applyTxs)
{
    auto built = std::make_shared<Ledger>(*parent, closeTime);

    // Set up to write SHAMap changes to our database,
    //   perform updates, extract changes

    {
        OpenView accum(&*built);
        assert(!accum.open());
        applyTxs(accum, built);
        accum.apply(*built);
    }

    built->updateSkipList();
    {
        // Write the final version of all modified SHAMap
        // nodes to the node store to preserve the new LCL

        int const asf = built->stateMap().flushDirty(
            hotACCOUNT_NODE, built->info().seq);
        int const tmf = built->txMap().flushDirty(
            hotTRANSACTION_NODE, built->info().seq);
        JLOG(j.debug()) << "Flushed " << asf << " accounts and " << tmf
                        << " transaction nodes";
    }
    built->unshare();

    // Accept ledger
    built->setAccepted(
        closeTime, closeResolution, closeTimeCorrect, app.config());

    return built;
}

/** Apply a set of consensus transactions to a ledger.

  @param app Handle to application
  @param txns the set of transactions to apply,
  @param failed set of transactions that failed to apply
  @param view ledger to apply to
  @param j Journal for logging
  @return number of transactions applied; transactions to retry left in txns
*/

std::size_t
applyTransactions(
    Application& app,
    std::shared_ptr<Ledger const> const& built,
    CanonicalTXSet& txns,
    std::set<TxID>& failed,
    OpenView& view,
    beast::Journal j)
{
    bool certainRetry = true;
    std::size_t count = 0;

    // Attempt to apply all of the retriable transactions
    for (int pass = 0; pass < LEDGER_TOTAL_PASSES; ++pass)
    {
        JLOG(j.debug())
            << (certainRetry ? "Pass: " : "Final pass: ") << pass
            << " begins (" << txns.size() << " transactions)";
        int changes = 0;

        auto it = txns.begin();

        while (it != txns.end())
        {
            auto const txid = it->first.getTXID();

            try
            {
                if (pass == 0 && built->txExists(txid))
                {
                    it = txns.erase(it);
                    continue;
                }

                switch (applyTransaction(
                    app, view, *it->second, certainRetry, tapNONE, j))
                {
                    case ApplyResult::Success:
                        it = txns.erase(it);
                        ++changes;
                        break;

                    case ApplyResult::Fail:
                        failed.insert(txid);
                        it = txns.erase(it);
                        break;

                    case ApplyResult::Retry:
                        ++it;
                }
            }
            catch (std::exception const&)
            {
                JLOG(j.warn()) << "Transaction " << txid << " throws";
                failed.insert(txid);
                it = txns.erase(it);
            }
        }

        JLOG(j.debug())
            << (certainRetry ? "Pass: " : "Final pass: ") << pass
            << " completed (" << changes << " changes)";

        // A non-retry pass made no changes
        if (!changes && !certainRetry)
            break;

        // Stop retriable passes
        if (!changes || (pass >= LEDGER_RETRY_PASSES))
            certainRetry = false;
    }

    // If there are any transactions left, we must have
    // tried them in at least one final pass
    assert(txns.empty() || !certainRetry);
    return count;
}

// Build a ledger from consensus transactions
std::shared_ptr<Ledger>
buildLedger(
    std::shared_ptr<Ledger const> const& parent,
    NetClock::time_point closeTime,
    const bool closeTimeCorrect,
    NetClock::duration closeResolution,
    Application& app,
    CanonicalTXSet& txns,
    std::set<TxID>& failedTxns,
    beast::Journal j)
{
    JLOG(j.debug()) << "Report: Transaction Set = " << txns.key()
                    << ", close " << closeTime.time_since_epoch().count()
                    << (closeTimeCorrect ? "" : " (incorrect)");

    return buildLedgerImpl(parent, closeTime, closeTimeCorrect,
        closeResolution, app, j,
        [&](OpenView& accum, std::shared_ptr<Ledger> const& built)
        {
            JLOG(j.debug())
                << "Attempting to apply " << txns.size()
                << " transactions";

            auto const applied = applyTransactions(app, built, txns,
                failedTxns, accum, j);

            if (txns.size() || txns.size())
                JLOG(j.debug())
                    << "Applied " << applied << " transactions; "
                    << failedTxns.size() << " failed and "
                    << txns.size() << " will be retried.";
            else
                JLOG(j.debug())
                    << "Applied " << applied
                    << " transactions.";
        });
}

// Build a ledger by replaying
std::shared_ptr<Ledger>
buildLedger(
    LedgerReplay const& replayData,
    ApplyFlags applyFlags,
    Application& app,
    beast::Journal j)
{
    auto const& replayLedger = replayData.replay();

    JLOG(j.debug()) << "Report: Replay Ledger " << replayLedger->info().hash;

    return buildLedgerImpl(
        replayData.parent(),
        replayLedger->info().closeTime,
        ((replayLedger->info().closeFlags & sLCF_NoConsensusTime) == 0),
        replayLedger->info().closeTimeResolution,
        app,
        j,
        [&](OpenView& accum, std::shared_ptr<Ledger> const& built)
        {
            for (auto& tx : replayData.orderedTxns())
                applyTransaction(app, accum, *tx.second, false, applyFlags, j);
        });
}

}  // namespace ripple
