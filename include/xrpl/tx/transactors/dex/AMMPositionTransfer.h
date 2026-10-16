#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** AMMPositionTransfer transfers ownership of a concentrated-liquidity
 *  position SLE from the source account to a named destination account.
 *
 *  The position keylet (`keylet::ammPosition`) is content-addressed by
 *  (ammID, original-creator, creation-seq) and therefore does NOT change
 *  on transfer — only the SLE's sfAccount field, its owner-directory
 *  page membership, and the source/destination owner reserves change.
 *  Subsequent AMMWithdraw / AMMCollectFees on this position keep using
 *  the same sfPositionID; the destination is the new authoritative owner.
 *
 *  Failure modes (sandbox semantics):
 *    - source != position.sfAccount        => tecNO_PERMISSION
 *    - position SLE missing / wrong type   => tecNO_ENTRY
 *    - destination account does not exist  => tecNO_DST
 *    - destination DepositAuth blocks src  => tecNO_PERMISSION
 *    - destination owner reserve full      => tecINSUFFICIENT_RESERVE
 *
 *  No trustline pre-check: if the destination later attempts to
 *  AMMWithdraw or AMMCollectFees and lacks a trustline for one of the
 *  pool assets, that transaction fails naturally with tecPATH_DRY /
 *  tecNO_LINE at fund-movement time. This matches the v3 NFT-transfer
 *  mental model where the position is moved as an opaque object.
 *
 *  No implicit fee collect: any accrued but uncollected fees travel with
 *  the position. If the source wants to bank fees before transfer, they
 *  submit AMMCollectFees first.
 */
class AMMPositionTransfer : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

    explicit AMMPositionTransfer(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static bool
    checkExtraFeatures(PreflightContext const& ctx);

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
