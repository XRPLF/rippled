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
 * @brief Creates a confidential Ballot with a homomorphically encrypted tally.
 *
 * A ballot references exactly one eligibility source: an MPT issuance
 * (token-weighted voting) or a permissioned domain (credential-gated, one
 * account one vote). Each per-option tally counter is initialized to a
 * canonical encryption of zero under the tally authority's ElGamal key, which
 * the creator registers with a Schnorr proof of key possession.
 *
 * @see BallotCastVote, BallotFinalize, BallotDelete
 */
class BallotCreate : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

    explicit BallotCreate(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

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
