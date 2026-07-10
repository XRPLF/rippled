#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <optional>
#include <tuple>

namespace xrpl {
class Sandbox;
class AMMClawback : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

    explicit AMMClawback(ApplyContext& ctx) : Transactor(ctx)
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

private:
    TER
    applyGuts(Sandbox& view);

    /** Withdraw both assets by providing maximum amount of asset1,
     * asset2's amount will be calculated according to the current proportion.
     * Since it is two-asset withdrawal, tfee is omitted.
     * @param view
     * @param ammAccount current AMM account
     * @param amountBalance current AMM asset1 balance
     * @param amount2Balance current AMM asset2 balance
     * @param lptAMMBalance current AMM LPT balance
     * @param amount asset1 withdraw amount
     * @return
     */
    std::tuple<TER, STAmount, STAmount, std::optional<STAmount>>
    equalWithdrawMatchingOneAmount(
        Sandbox& view,
        SLE const& ammSle,
        AccountID const& holder,
        AccountID const& ammAccount,
        STAmount const& amountBalance,
        STAmount const& amount2Balance,
        STAmount const& lptAMMBalance,
        STAmount const& holdLPtokens,
        STAmount const& amount);
};

}  // namespace xrpl
