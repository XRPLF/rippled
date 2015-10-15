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

#ifndef RIPPLE_APP_LEDGER_LEDGERCONSENSUS_H_INCLUDED
#define RIPPLE_APP_LEDGER_LEDGERCONSENSUS_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/ledger/LedgerProposal.h>
#include <ripple/app/ledger/InboundTransactions.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/CanonicalTXSet.h>
#include <ripple/app/misc/FeeVote.h>
#include <ripple/json/json_value.h>
#include <ripple/overlay/Peer.h>
#include <ripple/protocol/RippleLedgerHash.h>
#include <chrono>

namespace ripple {

/** Manager for achieving consensus on the next ledger.

    This object is created when the consensus process starts, and
    is destroyed when the process is complete.
*/
class LedgerConsensus
{
public:
    virtual ~LedgerConsensus() = default;

    virtual Json::Value getJson (bool full) = 0;

    virtual uint256 getLCL () = 0;

    virtual void mapComplete (uint256 const& hash,
        std::shared_ptr<SHAMap> const& map, bool acquired) = 0;

    virtual void timerEntry () = 0;

    virtual bool peerPosition (LedgerProposal::ref) = 0;

    /** Simulate the consensus process without any network traffic.

        The end result, is that consensus begins and completes as if everyone
        had agreed with whatever we propose.

        This function is only called from the rpc "ledger_accept" path with the
        server in standalone mode and SHOULD NOT be used during the normal
        consensus process.
    */
    virtual void simulate () = 0;
};

//------------------------------------------------------------------------------
/** Apply a set of transactions to a ledger

  @param set                   The set of transactions to apply
  @param applyLedger           The ledger to which the transactions should
                               be applied.
  @param checkLedger           A reference ledger for determining error
                               messages (typically new last closed
                                ledger).
  @param retriableTransactions collect failed transactions in this set
  @param openLgr               true if applyLedger is open, else false.
*/
void applyTransactions (
    Application& app,
    SHAMap const* set,
    OpenView& view,
    Ledger::ref checkLedger,
    CanonicalTXSet& retriableTransactions,
    ApplyFlags flags);

/** Apply a single transaction to a ledger
  @param view                   The open view to apply to
  @param txn                    The transaction to apply
  @param retryAssured           True if another pass is assured
  @param flags                  Flags for transactor
  @return                       resultSuccess, resultFail or resultRetry
*/
int applyTransaction (
    Application& app,
    OpenView& view,
    std::shared_ptr<STTx const> const& txn,
    bool retryAssured,
    ApplyFlags flags,
    beast::Journal j);

} // ripple

#endif
