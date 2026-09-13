#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

#include <memory>

namespace xrpl {

/**
 * @brief Deletes a Ballot or a BallotVote to reclaim its owner reserve.
 *
 * The ballot creator may delete a finalized ballot, or an unfinalized one past
 * its close time. A voter may delete their own BallotVote any time after the
 * ballot closes. BallotVote objects are independent of the Ballot's lifetime,
 * so deleting the ballot never orphans a voter's reserve.
 *
 * @see BallotCreate, BallotCastVote, BallotFinalize
 */
class BallotDelete : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

    explicit BallotDelete(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

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
