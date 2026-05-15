#pragma once

#include <xrpl/tx/Transactor.h>

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

    /** Helper function that might be needed by other transactors
     */
    static TER
    defaultLoan(
        ApplyView& view,
        SLE::ref loanSle,
        SLE::ref brokerSle,
        SLE::ref vaultSle,
        Asset const& vaultAsset,
        beast::Journal j);

    /** Helper function that might be needed by other transactors
     */
    static TER
    impairLoan(
        ApplyView& view,
        SLE::ref loanSle,
        SLE::ref vaultSle,
        Asset const& vaultAsset,
        beast::Journal j);

    /** Helper function that might be needed by other transactors
     */
    [[nodiscard]] static TER
    unimpairLoan(
        ApplyView& view,
        SLE::ref loanSle,
        SLE::ref vaultSle,
        Asset const& vaultAsset,
        beast::Journal j);

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

//------------------------------------------------------------------------------

}  // namespace xrpl
