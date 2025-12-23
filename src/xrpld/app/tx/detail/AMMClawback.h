#ifndef XRPL_TX_AMMCLAWBACK_H_INCLUDED
#define XRPL_TX_AMMCLAWBACK_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {
class Sandbox;
class AMMClawback : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit AMMClawback(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;

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

}  // namespace ripple

#endif
