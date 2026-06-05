#pragma once

#include <xrpld/app/main/Application.h>

#include <xrpl/beast/insight/Collector.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/protocol/RippleLedgerHash.h>

#include <optional>

namespace xrpl {

// VFALCO TODO Rename to OldLedgers ?

/** Retains historical ledgers. */
class LedgerHistory
{
public:
    LedgerHistory(beast::insight::Collector::ptr const& collector, Application& app);

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
        return ledgersByHash_.getHitRate();
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
        ledgersByHash_.sweep();
        consensusValidated_.sweep();
    }

    /** Report that we have locally built a particular ledger */
    void
    builtLedger(std::shared_ptr<Ledger const> const&, uint256 const& consensusHash, json::Value);

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
        json::Value const& consensus);

    Application& app_;
    beast::insight::Collector::ptr collector_;
    beast::insight::Counter mismatchCounter_;

    using LedgersByHash = TaggedCache<LedgerHash, Ledger const>;

    LedgersByHash ledgersByHash_;

    // Maps ledger indexes to the corresponding hashes
    // For debug and logging purposes
    struct CvEntry
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
        std::optional<json::Value> consensus;
    };
    using ConsensusValidated = TaggedCache<LedgerIndex, CvEntry>;
    ConsensusValidated consensusValidated_;

    // Maps ledger indexes to the corresponding hash.
    std::map<LedgerIndex, LedgerHash> ledgersByIndex_;  // validated ledgers

    beast::Journal j_;
};

}  // namespace xrpl
