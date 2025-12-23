#ifndef XRPL_TX_AMMWITHDRAW_H_INCLUDED
#define XRPL_TX_AMMWITHDRAW_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

#include <xrpl/ledger/View.h>

namespace ripple {

class Sandbox;

/** AMMWithdraw implements AMM withdraw Transactor.
 * The withdraw transaction is used to remove liquidity from the AMM instance
 * pool, thus redeeming some share of the pools that one owns in the form
 * of LPTokens. If the trader withdraws proportional values of both assets
 * without changing their relative pricing, no trading fee is charged on
 * the transaction. The trader can specify different combination of
 * the fields in the withdrawal.
 * LPTokens - transaction assumes proportional withdrawal of pool assets
 *     for the amount of LPTokens.
 * Asset1Out - transaction assumes withdrawal of single asset equivalent
 *     to the amount specified in Asset1Out.
 * Asset1Out and Asset2Out - transaction assumes all assets withdrawal
 *     with the constraints on the maximum amount of each asset that
 *     the trader is willing to withdraw.
 * Asset1Out and LPTokens - transaction assumes withdrawal of single
 *     asset specified in Asset1Out proportional to the share represented
 *     by the amount of LPTokens.
 * Asset1Out and EPrice - transaction assumes withdrawal of single
 *     asset with the following constraints:
 *         a. Amount of asset1 if specified (not 0) in Asset1Out specifies
 *             the minimum amount of asset1 that the trader is willing
 *             to withdraw.
 *         b. The effective price of asset traded out does not exceed
 *             the amount specified in EPrice.
 * Following updates after a successful transaction:
 * The withdrawn asset, if XRP, is transferred from AMM instance account
 *     to the account that initiated the transaction, thus changing
 *     the Balance field of each account.
 * The withdrawn asset, if token, is balanced between the AMM instance
 *     account and the issuer account.
 * The LPTokens ~  are balanced between the AMM instance account and
 *     the account that initiated the transaction.
 * The pool composition is updated.
 * @see [XLS30d:AMMWithdraw
 * transaction](https://github.com/XRPLF/XRPL-Standards/discussions/78)
 */

enum class WithdrawAll : bool { No = false, Yes };

class AMMWithdraw : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit AMMWithdraw(ApplyContext& ctx) : Transactor(ctx)
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

    /** Equal-asset withdrawal (LPTokens) of some AMM instance pools
     * shares represented by the number of LPTokens .
     * The trading fee is not charged.
     * @param view
     * @param ammAccount
     * @param amountBalance current LP asset1 balance
     * @param amount2Balance current LP asset2 balance
     * @param lptAMMBalance current AMM LPT balance
     * @param lpTokens current LPT balance
     * @param lpTokensWithdraw amount of tokens to withdraw
     * @param tfee trading fee in basis points
     * @param withdrawAll if withdrawing all lptokens
     * @param priorBalance balance before fees
     * @return
     */
    static std::tuple<TER, STAmount, STAmount, std::optional<STAmount>>
    equalWithdrawTokens(
        Sandbox& view,
        SLE const& ammSle,
        AccountID const account,
        AccountID const& ammAccount,
        STAmount const& amountBalance,
        STAmount const& amount2Balance,
        STAmount const& lptAMMBalance,
        STAmount const& lpTokens,
        STAmount const& lpTokensWithdraw,
        std::uint16_t tfee,
        FreezeHandling freezeHanding,
        WithdrawAll withdrawAll,
        XRPAmount const& priorBalance,
        beast::Journal const& journal);

    /** Withdraw requested assets and token from AMM into LP account.
     * Return new total LPToken balance and the withdrawn amounts for both
     * assets.
     * @param view
     * @param ammSle AMM ledger entry
     * @param ammAccount AMM account
     * @param amountBalance current LP asset1 balance
     * @param amountWithdraw asset1 withdraw amount
     * @param amount2Withdraw asset2 withdraw amount
     * @param lpTokensAMMBalance current AMM LPT balance
     * @param lpTokensWithdraw amount of lptokens to withdraw
     * @param tfee trading fee in basis points
     * @param withdrawAll if withdraw all lptokens
     * @param priorBalance balance before fees
     * @return
     */
    static std::tuple<TER, STAmount, STAmount, std::optional<STAmount>>
    withdraw(
        Sandbox& view,
        SLE const& ammSle,
        AccountID const& ammAccount,
        AccountID const& account,
        STAmount const& amountBalance,
        STAmount const& amountWithdraw,
        std::optional<STAmount> const& amount2Withdraw,
        STAmount const& lpTokensAMMBalance,
        STAmount const& lpTokensWithdraw,
        std::uint16_t tfee,
        FreezeHandling freezeHandling,
        WithdrawAll withdrawAll,
        XRPAmount const& priorBalance,
        beast::Journal const& journal);

    static std::pair<TER, bool>
    deleteAMMAccountIfEmpty(
        Sandbox& sb,
        std::shared_ptr<SLE> const ammSle,
        STAmount const& lpTokenBalance,
        Issue const& issue1,
        Issue const& issue2,
        beast::Journal const& journal);

private:
    std::pair<TER, bool>
    applyGuts(Sandbox& view);

    /** Withdraw requested assets and token from AMM into LP account.
     * Return new total LPToken balance.
     * @param view
     * @param ammSle AMM ledger entry
     * @param ammAccount AMM account
     * @param amountBalance current LP asset1 balance
     * @param amountWithdraw asset1 withdraw amount
     * @param amount2Withdraw asset2 withdraw amount
     * @param lpTokensAMMBalance current AMM LPT balance
     * @param lpTokensWithdraw amount of lptokens to withdraw
     * @return
     */
    std::pair<TER, STAmount>
    withdraw(
        Sandbox& view,
        SLE const& ammSle,
        AccountID const& ammAccount,
        STAmount const& amountBalance,
        STAmount const& amountWithdraw,
        std::optional<STAmount> const& amount2Withdraw,
        STAmount const& lpTokensAMMBalance,
        STAmount const& lpTokensWithdraw,
        std::uint16_t tfee);

    /** Equal-asset withdrawal (LPTokens) of some AMM instance pools
     * shares represented by the number of LPTokens .
     * The trading fee is not charged.
     * @param view
     * @param ammAccount
     * @param amountBalance current LP asset1 balance
     * @param amount2Balance current LP asset2 balance
     * @param lptAMMBalance current AMM LPT balance
     * @param lpTokens current LPT balance
     * @param lpTokensWithdraw amount of tokens to withdraw
     * @param tfee trading fee in basis points
     * @return
     */
    std::pair<TER, STAmount>
    equalWithdrawTokens(
        Sandbox& view,
        SLE const& ammSle,
        AccountID const& ammAccount,
        STAmount const& amountBalance,
        STAmount const& amount2Balance,
        STAmount const& lptAMMBalance,
        STAmount const& lpTokens,
        STAmount const& lpTokensWithdraw,
        std::uint16_t tfee);

    /** Withdraw both assets (Asset1Out, Asset2Out) with the constraints
     * on the maximum amount of each asset that the trader is willing
     * to withdraw. The trading fee is not charged.
     * @param view
     * @param ammAccount
     * @param amountBalance current AMM asset1 balance
     * @param amount2Balance current AMM asset2 balance
     * @param lptAMMBalance current AMM LPT balance
     * @param amount asset1 withdraw amount
     * @param amount2 max asset2 withdraw amount
     * @param tfee trading fee in basis points
     * @return
     */
    std::pair<TER, STAmount>
    equalWithdrawLimit(
        Sandbox& view,
        SLE const& ammSle,
        AccountID const& ammAccount,
        STAmount const& amountBalance,
        STAmount const& amount2Balance,
        STAmount const& lptAMMBalance,
        STAmount const& amount,
        STAmount const& amount2,
        std::uint16_t tfee);

    /** Single asset withdrawal (Asset1Out) equivalent to the amount specified
     * in Asset1Out. The trading fee is charged.
     * @param view
     * @param ammAccount
     * @param amountBalance current AMM asset1 balance
     * @param lptAMMBalance current AMM LPT balance
     * @param amount asset1 withdraw amount
     * @param tfee trading fee in basis points
     * @return
     */
    std::pair<TER, STAmount>
    singleWithdraw(
        Sandbox& view,
        SLE const& ammSle,
        AccountID const& ammAccount,
        STAmount const& amountBalance,
        STAmount const& lptAMMBalance,
        STAmount const& amount,
        std::uint16_t tfee);

    /** Single asset withdrawal (Asset1Out, LPTokens) proportional
     * to the share specified by tokens. The trading fee is charged.
     * @param view
     * @param ammAccount
     * @param amountBalance current AMM asset1 balance
     * @param lptAMMBalance current AMM LPT balance
     * @param amount asset1 withdraw amount
     * @param lpTokensWithdraw amount of tokens to withdraw
     * @param tfee trading fee in basis points
     * @return
     */
    std::pair<TER, STAmount>
    singleWithdrawTokens(
        Sandbox& view,
        SLE const& ammSle,
        AccountID const& ammAccount,
        STAmount const& amountBalance,
        STAmount const& lptAMMBalance,
        STAmount const& amount,
        STAmount const& lpTokensWithdraw,
        std::uint16_t tfee);

    /** Withdraw single asset (Asset1Out, EPrice) with two constraints.
     * The trading fee is charged.
     * @param view
     * @param ammAccount
     * @param amountBalance current AMM asset1 balance
     * @param lptAMMBalance current AMM LPT balance
     * @param amount asset1 withdraw amount
     * @param ePrice maximum asset1 effective price
     * @param tfee trading fee in basis points
     * @return
     */
    std::pair<TER, STAmount>
    singleWithdrawEPrice(
        Sandbox& view,
        SLE const& ammSle,
        AccountID const& ammAccount,
        STAmount const& amountBalance,
        STAmount const& lptAMMBalance,
        STAmount const& amount,
        STAmount const& ePrice,
        std::uint16_t tfee);

    /** Check from the flags if it's withdraw all */
    static WithdrawAll
    isWithdrawAll(STTx const& tx);
};

}  // namespace ripple

#endif  // XRPL_TX_AMMWITHDRAW_H_INCLUDED
