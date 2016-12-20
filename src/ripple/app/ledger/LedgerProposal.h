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

#ifndef RIPPLE_APP_LEDGER_LEDGERPROPOSAL_H_INCLUDED
#define RIPPLE_APP_LEDGER_LEDGERPROPOSAL_H_INCLUDED

#include <ripple/basics/CountedObject.h>
#include <ripple/basics/base_uint.h>
#include <ripple/json/json_value.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/beast/hash/hash_append.h>
#include <ripple/consensus/ConsensusProposal.h>
#include <chrono>
#include <cstdint>
#include <string>

namespace ripple {

/** A potentially signed ConsensusProposal for use in RCLConsensus.

    A signed
*/
class LedgerProposal
    : public CountedObject <LedgerProposal>
    , public ConsensusProposal<NodeID, uint256, uint256, NetClock::time_point>
{
    using Base =
        ConsensusProposal<NodeID, uint256, uint256, NetClock::time_point>;


public:
    static char const* getCountedObjectName () { return "LedgerProposal"; }
    using pointer = std::shared_ptr<LedgerProposal>;
    using ref = const pointer&;

    /** Constructor (Peer)

        Constructs a peer's ledger proposal.

        @param prevLedger The previous ledger this proposal is building on.
        @param proposeSeq The sequence number of this proposal.
        @param propose The position taken on transactions in this round.
        @param closeTime Position of when this ledger closed.
        @param now Time when the proposal was taken.
        @param publicKey Public key of the peer
        @param nodeID ID of node/peer taking this position.
        @param signature Signature provided with the proposal
        @param suppress
    */

    LedgerProposal (
        uint256 const& prevLedger,
        std::uint32_t proposeSeq,
        uint256 const& propose,
        NetClock::time_point closeTime,
        NetClock::time_point now,
        PublicKey const& publicKey,
        NodeID const& nodeID,
        Slice const& signature,
        uint256 const& suppress);

    /** Constructor (Self)

        Constructs our own ledger proposal.

        @param prevLedger The previous ledger this proposal is building on.
        @param position The position taken on transactions in this round.
        @param closeTime Position of when this ledger closed.
        @param now Time when the proposal was taken.
        @param nodeID Our ID
    */
    LedgerProposal (
        uint256 const& prevLedger,
        uint256 const& position,
        NetClock::time_point closeTime,
        NetClock::time_point now,
        NodeID const& nodeID);

    //! Create the signing hash for the proposal
    uint256 getSigningHash () const;

    //! Verify the signing hash of the proposal
    bool checkSign () const;

    //! Signature of the proposal (not necessarily verified)
    Blob const& getSignature () const
    {
        return signature_;
    }

    //! Public key of peer that sent the proposal
    PublicKey const& getPublicKey () const
    {
        return publicKey_;
    }

    //! ?????
    uint256 const& getSuppressionID () const
    {
        return mSuppression;
    }

    //! JSON representation of proposal
    Json::Value getJson () const;

private:
    template <class Hasher>
    void
    hash_append (Hasher& h) const
    {
        using beast::hash_append;
        hash_append(h, HashPrefix::proposal);
        hash_append(h, std::uint32_t(proposeSeq()));
        hash_append(h, closeTime());
        hash_append(h, prevLedger());
        hash_append(h, position());
    }

    uint256 mSuppression;
    PublicKey publicKey_;
    Blob signature_;
};

/** Calculate a unique identifier for a signed proposal.

    The identifier is based on all the fields that contribute to the signature,
    as well as the signature itself. The "last closed ledger" field may be
    omitted, but the signer will compute the signature as if this field was
    present. Recipients of the proposal will inject the last closed ledger in
    order to validate the signature. If the last closed ledger is left out, then
    it is considered as all zeroes for the purposes of signing.

    @param proposeHash The hash of the proposed position
    @param previousLedger The hash of the ledger the proposal is based upon
    @param proposeSeq Sequence number of the proposal
    @param closeTime Close time of the proposal
    @param publicKey Signer's public key
    @param signature Proposal signature
*/
uint256 proposalUniqueId (
        uint256 const& proposeHash,
        uint256 const& previousLedger,
        std::uint32_t proposeSeq,
        NetClock::time_point closeTime,
        Slice const& publicKey,
        Slice const& signature);

} // ripple

#endif
