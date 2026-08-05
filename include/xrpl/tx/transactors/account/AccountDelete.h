#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

/**
 * Check that an account satisfies the preconditions for deletion.
 *
 * Verifies there are no NFT obligations, the account is not sponsoring others,
 * the account's sequence is outside the 256-ledger replay window, and every
 * object it owns is a non-obligation deletable type. When the account carries a
 * reserve sponsor (sfSponsor), @p dst must be that sponsor.
 *
 * Shared by AccountDelete::preclaim and the SponsorshipTransfer reap path so
 * both enforce identical deletion safety.
 */
[[nodiscard]] TER
checkAccountDeletable(
    ReadView const& view,
    SLE::const_ref sleAccount,
    AccountID const& dst,
    beast::Journal j);

/**
 * Delete @p src, sweeping its residual XRP to @p dst.
 *
 * Tears down every non-obligation object @p src owns, transfers its remaining
 * balance to @p dst, unwinds any reserve sponsorship (decrementing the
 * sponsor's sfSponsoringAccountCount and clearing sfSponsor), removes the owner
 * directory, and erases the account root. Callers must have already confirmed
 * the account is deletable via checkAccountDeletable. Shared by
 * AccountDelete::doApply and the SponsorshipTransfer reap path.
 */
[[nodiscard]] TER
applyAccountDelete(ApplyContext& ctx, SLE::pointer src, SLE::pointer dst, beast::Journal j);

class AccountDelete : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Blocker;

    explicit AccountDelete(ApplyContext& ctx) : Transactor(ctx)
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
    visitInvariantEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after) override;

    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;
};

}  // namespace xrpl
