//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_TX_AMMWITHDRAW_H_INCLUDED
#define RIPPLE_TX_AMMWITHDRAW_H_INCLUDED

#include <ripple/app/tx/impl/Transactor.h>

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
class AMMWithdraw : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit AMMWithdraw(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;

private:
    std::pair<TER, bool>
    applyGuts(Sandbox& view);

    /** Withdraw requested assets and token from AMM into LP account.
     * Return new total LPToken balance.
     * @param view
     * @param ammAccount
     * @param amountBalance
     * @param amountWithdraw
     * @param amount2Withdraw
     * @param lpTokensAMMBalance current AMM LPT balance
     * @param lpTokensWithdraw
     * @param tfee
     * @return
     */
    std::pair<TER, STAmount>
    withdraw(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& amountWithdraw,
        STAmount const& amountBalance,
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
        AccountID const& ammAccount,
        STAmount const& amountBalance,
        STAmount const& lptAMMBalance,
        STAmount const& amount,
        STAmount const& ePrice,
        std::uint16_t tfee);
};

}  // namespace ripple

#endif  // RIPPLE_TX_AMMWITHDRAW_H_INCLUDED
