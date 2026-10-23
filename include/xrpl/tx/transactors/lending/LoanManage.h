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
     * Vault, capped at the LoanBroker's CoverAvailable and floored so that AssetsAvailable + cover
     * is exactly on its posterior grid.
     *
     * The LoanBroker absorbs the rounding. When CoverAvailable - cover needs finer digits than the
     * broker's posterior grid, it rounds to nearest, as the broker's trust line does, so
     * CoverAvailable can move by up to half a posterior ulp more or less than the cover. When the
     * two grids differ, no cover is exact on both sides; the Vault side is kept exact.
     *
     * The one exception: when AssetsAvailable has digits finer than the 16-digit cover can carry,
     * no cover cancels them, and the balance writer rounds AssetsAvailable + cover to nearest,
     * by at most half a posterior ulp. VaultDeposit and LoanPay credits have the same limit.
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
