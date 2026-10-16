#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <memory>

namespace xrpl {

/**
 * @brief Casts an encrypted, zero-knowledge-proven vote onto a Ballot.
 *
 * Every cast submits a vector of N ciphertexts (one per option) under the
 * tally key: the chosen option(s) encrypt the voter's weight, the rest encrypt
 * zero. Validators homomorphically add the vector into the ballot's encrypted
 * tally, so the running count stays hidden. The cast proves each option value
 * is non-negative (aggregated Bulletproof) and that the vector sums to the
 * voter's public weight W.
 *
 * @note v1 crypto: range and sum-to-W are verified against the merged
 *       mpt-crypto 0.4.0-rc4 primitives. The per-option ciphertext-commitment
 *       linkage requires a verifiable-encryption primitive not yet exported by
 *       the library; credential-mode casts are gated (rejected) on it. See
 *       plan-2-implementation.md.
 *
 * @see BallotCreate, BallotFinalize
 */
class BallotCastVote : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

    explicit BallotCastVote(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;

    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;
};

}  // namespace xrpl
