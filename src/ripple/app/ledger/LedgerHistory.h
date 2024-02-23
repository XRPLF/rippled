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

#ifndef RIPPLE_APP_LEDGER_LEDGERHISTORY_H_INCLUDED
#define RIPPLE_APP_LEDGER_LEDGERHISTORY_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/main/Application.h>
#include <ripple/beast/insight/Collector.h>
#include <ripple/beast/insight/Event.h>
#include <ripple/protocol/RippleLedgerHash.h>

#include <optional>

namespace ripple {

// VFALCO TODO Rename to OldLedgers ?

/** Retains historical ledgers. */
class LedgerHistory
{
public:
    LedgerHistory(
        beast::insight::Collector::ptr const& collector,
        Application& app);

    /** Track a ledger
        @return `true` if the ledger was already tracked
    */
    bool
    insert(std::shared_ptr<Ledger const> const& ledger, bool validated);

    /** Get the ledgers_by_hash cache hit rate
        @return the hit rate
    */
    float
    getCacheHitRate()
    {
        return m_ledgers_by_hash.getHitRate();
    }

    /** Get a ledger given its sequence number */
    std::shared_ptr<Ledger const>
    getLedgerBySeq(LedgerIndex ledgerIndex);

    /** Retrieve a ledger given its hash */
    std::shared_ptr<Ledger const>
    getLedgerByHash(LedgerHash const& ledgerHash);

    /** Get a ledger's hash given its sequence number
        @param ledgerIndex The sequence number of the desired ledger
        @return The hash of the specified ledger
    */
    LedgerHash
    getLedgerHash(LedgerIndex ledgerIndex);

    /** Remove stale cache entries
     */
    void
    sweep()
    {
        m_ledgers_by_hash.sweep();
        m_consensus_validated.sweep();
    }

    /** Report that we have locally built a particular ledger */
    void
    builtLedger(
        std::shared_ptr<Ledger const> const&,
        uint256 const& consensusHash,
        Json::Value);

    /** Report that we have validated a particular ledger */
    void
    validatedLedger(
        std::shared_ptr<Ledger const> const&,
        std::optional<uint256> const& consensusHash);

    /** Repair a hash to index mapping
        @param ledgerIndex The index whose mapping is to be repaired
        @param ledgerHash The hash it is to be mapped to
        @return `false` if the mapping was repaired
    */
    bool
    fixIndex(LedgerIndex ledgerIndex, LedgerHash const& ledgerHash);

    void
    clearLedgerCachePrior(LedgerIndex seq);

private:
    /** Log details in the case where we build one ledger but
        validate a different one.
        @param built The hash of the ledger we built
        @param valid The hash of the ledger we deemed fully valid
        @param builtConsensusHash The hash of the consensus transaction for the
        ledger we built
        @param validatedConsensusHash The hash of the validated ledger's
        consensus transaction set
        @param consensus The status of the consensus round
    */
    void
    handleMismatch(
        LedgerHash const& built,
        LedgerHash const& valid,
        std::optional<uint256> const& builtConsensusHash,
        std::optional<uint256> const& validatedConsensusHash,
        Json::Value const& consensus);

    Application& app_;
    beast::insight::Collector::ptr collector_;
    beast::insight::Counter mismatch_counter_;

    using LedgersByHash = TaggedCache<LedgerHash, Ledger const>;

    LedgersByHash m_ledgers_by_hash;

    // Maps ledger indexes to the corresponding hashes
    // For debug and logging purposes
    struct cv_entry
    {
        // Hash of locally built ledger
        std::optional<LedgerHash> built;
        // Hash of the validated ledger
        std::optional<LedgerHash> validated;
        // Hash of locally accepted consensus transaction set
        std::optional<uint256> builtConsensusHash;
        // Hash of validated consensus transaction set
        std::optional<uint256> validatedConsensusHash;
        // Consensus metadata of built ledger
        std::optional<Json::Value> consensus;
    };
    using ConsensusValidated = TaggedCache<LedgerIndex, cv_entry>;
    ConsensusValidated m_consensus_validated;

    // Maps ledger indexes to the corresponding hash.
    std::map<LedgerIndex, LedgerHash> mLedgersByIndex;  // validated ledgers

    beast::Journal j_;
};

}  // namespace ripple

#endif
