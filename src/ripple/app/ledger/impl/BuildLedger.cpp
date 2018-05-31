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
    auto buildLCL = std::make_shared<Ledger>(*parent, closeTime);

    if (buildLCL->rules().enabled(featureSHAMapV2) &&
        !buildLCL->stateMap().is_v2())
    {
        buildLCL->make_v2();
    }

    // Set up to write SHAMap changes to our database,
    //   perform updates, extract changes

    {
        OpenView accum(&*buildLCL);
        assert(!accum.open());
        applyTxs(accum, buildLCL);
        accum.apply(*buildLCL);
    }

    buildLCL->updateSkipList();

    {
        // Write the final version of all modified SHAMap
        // nodes to the node store to preserve the new LCL

        int const asf = buildLCL->stateMap().flushDirty(
            hotACCOUNT_NODE, buildLCL->info().seq);
        int const tmf = buildLCL->txMap().flushDirty(
            hotTRANSACTION_NODE, buildLCL->info().seq);
        JLOG(j.debug()) << "Flushed " << asf << " accounts and " << tmf
                        << " transaction nodes";
    }
    buildLCL->unshare();

    // Accept ledger
    buildLCL->setAccepted(
        closeTime, closeResolution, closeTimeCorrect, app.config());

    return buildLCL;
}

/** Apply a set of consensus transactions to a ledger.

  @param app Handle to application
  @param txns Consensus transactions to apply
  @param view Ledger to apply to
  @param buildLCL Ledger to check if transaction already exists
  @param j Journal for logging
  @return Any retriable transactions
*/

CanonicalTXSet
applyTransactions(
    Application& app,
    SHAMap const& txns,
    OpenView& view,
    std::shared_ptr<Ledger> const& buildLCL,
    beast::Journal j)
{
    CanonicalTXSet retriableTxs(txns.getHash().as_uint256());

    for (auto const& item : txns)
    {
        if (buildLCL->txExists(item.key()))
            continue;

        // The transaction wasn't filtered
        // Add it to the set to be tried in canonical order
        JLOG(j.debug()) << "Processing candidate transaction: " << item.key();
        try
        {
            retriableTxs.insert(
                std::make_shared<STTx const>(SerialIter{item.slice()}));
        }
        catch (std::exception const&)
        {
            JLOG(j.warn()) << "Txn " << item.key() << " throws";
        }
    }

    bool certainRetry = true;
    // Attempt to apply all of the retriable transactions
    for (int pass = 0; pass < LEDGER_TOTAL_PASSES; ++pass)
    {
        JLOG(j.debug()) << "Pass: " << pass << " Txns: " << retriableTxs.size()
                        << (certainRetry ? " retriable" : " final");
        int changes = 0;

        auto it = retriableTxs.begin();

        while (it != retriableTxs.end())
        {
            try
            {
                switch (applyTransaction(
                    app, view, *it->second, certainRetry, tapNONE, j))
                {
                    case ApplyResult::Success:
                        it = retriableTxs.erase(it);
                        ++changes;
                        break;

                    case ApplyResult::Fail:
                        it = retriableTxs.erase(it);
                        break;

                    case ApplyResult::Retry:
                        ++it;
                }
            }
            catch (std::exception const&)
            {
                JLOG(j.warn()) << "Transaction throws";
                it = retriableTxs.erase(it);
            }
        }

        JLOG(j.debug()) << "Pass: " << pass << " finished " << changes
                        << " changes";

        // A non-retry pass made no changes
        if (!changes && !certainRetry)
            return retriableTxs;

        // Stop retriable passes
        if (!changes || (pass >= LEDGER_RETRY_PASSES))
            certainRetry = false;
    }

    // If there are any transactions left, we must have
    // tried them in at least one final pass
    assert(retriableTxs.empty() || !certainRetry);
    return retriableTxs;
}

// Build a ledger from consensus transactions
std::shared_ptr<Ledger>
buildLedger(
    std::shared_ptr<Ledger const> const& parent,
    NetClock::time_point closeTime,
    const bool closeTimeCorrect,
    NetClock::duration closeResolution,
    SHAMap const& txs,
    Application& app,
    CanonicalTXSet& retriableTxs,
    beast::Journal j)
{
    JLOG(j.debug()) << "Report: TxSt = " << txs.getHash().as_uint256()
                    << ", close " << closeTime.time_since_epoch().count()
                    << (closeTimeCorrect ? "" : " (incorrect)");

    return buildLedgerImpl(
        parent,
        closeTime,
        closeTimeCorrect,
        closeResolution,
        app,
        j,
        [&](OpenView& accum, std::shared_ptr<Ledger> const& buildLCL) {
            retriableTxs = applyTransactions(app, txs, accum, buildLCL, j);
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
        [&](OpenView& accum, std::shared_ptr<Ledger> const& buildLCL) {
            for (auto& tx : replayData.orderedTxns())
                applyTransaction(app, accum, *tx.second, false, applyFlags, j);
        });
}

}  // namespace ripple
