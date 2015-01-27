//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-14 Ripple Labs Inc.

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

#ifndef RIPPLE_TXHOLD_H_INCLUDED
#define RIPPLE_TXHOLD_H_INCLUDED

#include <boost/intrusive/set.hpp>

#include <ripple/core/Config.h>
#include <ripple/core/impl/LoadFeeTrackImp.h>
#include <ripple/protocol/TER.h>
#include <ripple/protocol/STTx.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/ledger/LedgerEntrySet.h>

namespace ripple {

class TransactionEngine;

class TxQ
{
public:
    struct Setup
    {
        size_t ledgersInQueue_ = 20;
        size_t minLedgersToComputeSizeLimit_ = ledgersInQueue_ * 10;
        size_t maxLedgerCountsToStore_ = minLedgersToComputeSizeLimit_ * 5;
    };

    struct TxFeeMetrics
    {
        int txCount;            // Transactions in the queue
        int txPerLedger;        // Amount expected per ledger
        int referenceFeeLevel;  // Reference transaction fee level
        int minFeeLevel;        // Minimum fee level to get in the queue
        int medFeeLevel;        // Median fee level of the last ledger
        int expFeeLevel;        // Estimated fee level to get in next ledger
    };

    enum TxDisposition
    {
        TD_malformed,     // Transaction is broken
        TD_superceded,    // Transaction can never succeed on network
        TD_low_fee,       // Fee is too low
        TD_failed,        // Not likely to claim a fee
        TD_missing_prior, // Dependent on non-present transaction
        TD_held,          // Waiting for emptier ledger
        TD_open_ledger    // Placed in the open ledger
    };

    // ALMOST DEFINITELY THE WRONG TER CODES
    static const TER txnResultHeld;
    static const TER txnResultLowFee;

    virtual ~TxQ () { }

    // Add a new transaction to the open ledger or hold
    // (Or reject it)
    virtual std::pair <TxDisposition, TER>
        addTransaction (
            STTx::ref txn,
            TransactionEngineParams params,
            TransactionEngine& engine) = 0;

    // Fill the new open ledger with transactions from the hold
    virtual void fillOpenLedger(TransactionEngine&) = 0;

    // We have a new last validated ledger, update the hold
    virtual void processValidatedLedger (Ledger::ref) = 0;

    virtual struct TxFeeMetrics getFeeMetrics () = 0;

    virtual Json::Value doRPC(Json::Value const& query) = 0;
};

TxQ::Setup
setup_TxQ(Config const& c);

std::unique_ptr<TxQ>
make_TxQ(TxQ::Setup const& setup,
    LoadFeeTrack& lft, beast::Journal journal);



} // ripple

#endif
