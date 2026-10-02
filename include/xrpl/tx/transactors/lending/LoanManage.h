#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>

namespace xrpl {

class LoanManage : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

    explicit LoanManage(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    /**
     * Helper function that might be needed by other transactors
     */
    static TER
    defaultLoan(
        ApplyView& view,
        SLE::Ref loanSle,
        SLE::Ref brokerSle,
        SLE::Ref vaultSle,
        beast::Journal j);

    /**
     * @brief Compute the First-Loss Capital cover amount for a defaulted Loan on a FixedPrecision
     * Vault, capped at the LoanBroker's CoverAvailable and rounded toward zero at the coarser of
     * the vault's AssetsAvailable and the broker's CoverAvailable posterior grids.
     * CoverAvailable - cover is exactly representable; AssetsAvailable + cover is rounded by the
     * balance writer.
     *
     * When AssetsAvailable is on a finer grid than the posterior sum, that rounding (to nearest,
     * as the trust line or MPT transfer does) can credit the Vault up to half a posterior ulp more
     * than the cover the broker pays. The cover must stay on the broker's grid, so no cover can
     * remove this. It is accepted: AssetsAvailable still equals the pseudo-account balance, and
     * the extra amount is below the Vault's own precision.
     * @param loanSle The Loan being defaulted.
     * @param brokerSle The Loan's LoanBroker.
     * @param vaultSle The LoanBroker's Vault.
     * @return The cover amount, in the Vault's Asset.
     */
    static STAmount
    calculateDefaultCover(SLE::Ref loanSle, SLE::Ref brokerSle, SLE::Ref vaultSle);

    /**
     * Helper function that might be needed by other transactors
     */
    static TER
    impairLoan(
        ApplyView& view,
        SLE::Ref loanSle,
        SLE::Ref vaultSle,
        Asset const& vaultAsset,
        beast::Journal j);

    /**
     * Helper function that might be needed by other transactors
     */
    [[nodiscard]] static TER
    unimpairLoan(
        ApplyView& view,
        SLE::Ref loanSle,
        SLE::Ref vaultSle,
        Asset const& vaultAsset,
        beast::Journal j);

    TER
    doApply() override;

    void
    visitInvariantEntry(bool isDelete, SLE::ConstRef before, SLE::ConstRef after) override;

    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;
};

//------------------------------------------------------------------------------

}  // namespace xrpl
