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

#ifndef RIPPLE_LEDGERCONSENSUS_H
#define RIPPLE_LEDGERCONSENSUS_H

namespace ripple {
    
/** Manager for achieving consensus on the next ledger.

    This object is created when the consensus process starts, and
    is destroyed when the process is complete.
*/
class LedgerConsensus
{
public:    
    typedef beast::abstract_clock <std::chrono::seconds> clock_type;

    virtual ~LedgerConsensus() = 0;

    virtual int startup () = 0;

    virtual Json::Value getJson (bool full) = 0;

    virtual Ledger::ref peekPreviousLedger () = 0;

    virtual uint256 getLCL () = 0;

    virtual SHAMap::pointer getTransactionTree (uint256 const & hash, 
        bool doAcquire) = 0;

    virtual void mapComplete (uint256 const & hash, SHAMap::ref map, 
        bool acquired) = 0;

    virtual bool stillNeedTXSet (uint256 const & hash) = 0;

    virtual void checkLCL () = 0;

    virtual void handleLCL (uint256 const & lclHash) = 0;

    virtual void timerEntry () = 0;

    // state handlers
    virtual void statePreClose () = 0;
    virtual void stateEstablish () = 0;
    virtual void stateFinished () = 0;
    virtual void stateAccepted () = 0;

    virtual bool haveConsensus (bool forReal) = 0;

    virtual bool peerPosition (LedgerProposal::ref) = 0;

    virtual bool peerHasSet (Peer::ptr const& peer, uint256 const & set,
        protocol::TxSetStatus status) = 0;

    virtual SHAMapAddNode peerGaveNodes (Peer::ptr const& peer, 
        uint256 const & setHash,
        const std::list<SHAMapNode>& nodeIDs, 
        const std::list< Blob >& nodeData) = 0;

    virtual bool isOurPubKey (const RippleAddress & k) = 0;

    // test/debug
    virtual void simulate () = 0;
};

std::shared_ptr <LedgerConsensus>
make_LedgerConsensus (LedgerConsensus::clock_type& clock, LocalTxs& localtx,
    LedgerHash const & prevLCLHash, Ledger::ref previousLedger,
        std::uint32_t closeTime, FeeVote& feeVote);

} // ripple

#endif
