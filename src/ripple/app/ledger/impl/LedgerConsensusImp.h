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

#ifndef RIPPLE_APP_LEDGER_IMPL_LEDGERCONSENSUSIMP_H_INCLUDED
#define RIPPLE_APP_LEDGER_IMPL_LEDGERCONSENSUSIMP_H_INCLUDED

#include <BeastConfig.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/impl/ConsensusImp.h>
#include <ripple/consensus/DisputedTx.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/CanonicalTXSet.h>
#include <ripple/app/misc/FeeVote.h>
#include <ripple/basics/CountedObject.h>
#include <ripple/protocol/STValidation.h>
#include <ripple/protocol/UintTypes.h>

namespace ripple {

/**
  Provides the implementation for LedgerConsensus.

  Achieves consensus on the next ledger.

  Two things need consensus:
    1.  The set of transactions.
    2.  The close time for the ledger.
*/
template <class Traits>
class LedgerConsensusImp
    : public LedgerConsensus<Traits>
    , public std::enable_shared_from_this <LedgerConsensusImp<Traits>>
    , public CountedObject <LedgerConsensusImp<Traits>>
{
public:

    using typename Traits::Time_t;
    using typename Traits::Pos_t;
    using typename Traits::TxSet_t;
    using typename Traits::Tx_t;
    using typename Traits::LgrID_t;
    using typename Traits::TxID_t;
    using typename Traits::TxSetID_t;
    using typename Traits::NodeID_t;
    using Dispute_t = DisputedTx <Tx_t, NodeID_t>;

    /**
     * The result of applying a transaction to a ledger.
    */
    enum {resultSuccess, resultFail, resultRetry};

    static char const* getCountedObjectName () { return "LedgerConsensus"; }

    LedgerConsensusImp(LedgerConsensusImp const&) = delete;
    LedgerConsensusImp& operator=(LedgerConsensusImp const&) = delete;

    ~LedgerConsensusImp () = default;


    /**
        @param localtx transactions issued by local clients
        @param inboundTransactions set of inbound transaction sets
        @param localtx A set of local transactions to apply
        @param feeVote Our desired fee levels and voting logic.
    */
    LedgerConsensusImp (
        Application& app,
        ConsensusImp& consensus,
        InboundTransactions& inboundTransactions,
        LocalTxs& localtx,
        LedgerMaster& ledgerMaster,
        FeeVote& feeVote);

    /* The hash of the last closed ledger */
    LgrID_t getLCL () override;

private:

    /** Check if we've reached consensus */
    bool haveConsensus ();


    /** We have a new last closed ledger, process it. Final accept logic

      @param set Our consensus set
    */
    void accept (TxSet_t const& set);

    /** Send a node status change message to our directly connected peers

      @param event   The event which caused the status change.  This is
                     typically neACCEPTED_LEDGER or neCLOSING_LEDGER.
      @param ledger  The ledger associated with the event.
    */
    void statusChange (protocol::NodeEvent event, ReadView const& ledger);


    /**
       Called while trying to avalanche towards consensus.
       Adjusts our positions to try to agree with other validators.
    */
    void updateOurPositions ();

    /** We have a new LCL and must accept it */
    void beginAccept (bool synchronous);

    void endConsensus (bool correctLCL);


    /** Add our load fee to our validation */
    void addLoad(STValidation::ref val);

    /** Convert an advertised close time to an effective close time */
    NetClock::time_point effectiveCloseTime(NetClock::time_point closeTime);
};

//------------------------------------------------------------------------------

std::shared_ptr <LedgerConsensus <RCLCxTraits>>
make_LedgerConsensus (
    Application& app,
    ConsensusImp& consensus,
    InboundTransactions& inboundTransactions,
    LocalTxs& localtx,
    LedgerMaster& ledgerMaster,
    FeeVote& feeVote);

//------------------------------------------------------------------------------
/** Apply a set of transactions to a ledger

  Typically the txFilter is used to reject transactions
  that already got in the prior ledger

  @param set            set of transactions to apply
  @param view           ledger to apply to
  @param txFilter       callback, return false to reject txn
  @return               retriable transactions
*/
CanonicalTXSet
applyTransactions (
    Application& app,
    RCLTxSet const& set,
    OpenView& view,
    std::function<bool(uint256 const&)> txFilter);

extern template class LedgerConsensusImp <RCLCxTraits>;

} // ripple

#endif
