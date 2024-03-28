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

#ifndef RIPPLE_TX_AMMDEPOSIT_H_INCLUDED
#define RIPPLE_TX_AMMDEPOSIT_H_INCLUDED

#include <ripple/app/tx/impl/Transactor.h>

namespace ripple {

class Sandbox;

/** AMMDeposit implements AMM deposit Transactor.
 * The deposit transaction is used to add liquidity to the AMM instance pool,
 * thus obtaining some share of the instance's pools in the form of LPTokens.
 * If the trader deposits proportional values of both assets without changing
 * their relative price, then no trading fee is charged on the transaction.
 * The trader can specify different combination of the fields in the deposit.
 * LPTokens - transaction assumes proportional deposit of pools assets in
 *     exchange for the specified amount of LPTokens of the AMM instance.
 * Asset1In - transaction assumes single asset deposit of the amount of asset
 *     specified by Asset1In. This is essentially a swap and an equal asset
 *     deposit.
 * Asset1In and Asset2In - transaction assumes proportional deposit of pool
 *     assets with the constraints on the maximum amount of each asset that
 *     the trader is willing to deposit.
 * Asset1In and LPTokens - transaction assumes that a single asset asset1
 *     is deposited to obtain some share of the AMM instance's pools
 *     represented by amount of LPTokens.
 * Asset1In and EPrice - transaction assumes single asset deposit with
 *     the following two constraints:
 *         a. amount of asset1 if specified (not 0) in Asset1In specifies the
 * maximum amount of asset1 that the trader is willing to deposit b. The
 * effective-price of the LPTokens traded out does not exceed the specified
 * EPrice. Following updates after a successful AMMDeposit transaction: The
 * deposited asset, if XRP, is transferred from the account that initiated the
 * transaction to the AMM instance account, thus changing the Balance field of
 * each account. The deposited asset, if tokens, are balanced between the AMM
 * account and the issuer account trustline. The LPTokens are issued by the AMM
 * instance account to the account that initiated the transaction and a new
 * trustline is created, if there does not exist one. The pool composition is
 * updated.
 * @see [XLS30d:AMMDeposit
 * transaction](https://github.com/XRPLF/XRPL-Standards/discussions/78)
 */
class AMMDeposit : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit AMMDeposit(ApplyContext& ctx) : Transactor(ctx)
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

    /** Deposit requested assets and token amount into LP account.
     * Return new total LPToken balance.
     * @param view
     * @param ammAccount
     * @param amountBalance current AMM asset1 balance
     * @param amountDeposit
     * @param amount2Deposit
     * @param lptAMMBalance current AMM LPT balance
     * @param lpTokensDeposit amount of tokens to deposit
     * @param depositMin minimum accepted amount deposit
     * @param deposit2Min minimum accepted amount2 deposit
     * @param lpTokensDepositMin minimum accepted LPTokens deposit
     * @param tfee trading fee in basis points
     * @return
     */
    std::pair<TER, STAmount>
    deposit(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& amountBalance,
        STAmount const& amountDeposit,
        std::optional<STAmount> const& amount2Deposit,
        STAmount const& lptAMMBalance,
        STAmount const& lpTokensDeposit,
        std::optional<STAmount> const& depositMin,
        std::optional<STAmount> const& deposit2Min,
        std::optional<STAmount> const& lpTokensDepositMin,
        std::uint16_t tfee);

    /** Equal asset deposit (LPTokens) for the specified share of
     * the AMM instance pools. The trading fee is not charged.
     * @param view
     * @param ammAccount
     * @param amountBalance current AMM asset1 balance
     * @param amount2Balance current AMM asset2 balance
     * @param lptAMMBalance current AMM LPT balance
     * @param lpTokensDeposit amount of tokens to deposit
     * @param depositMin minimum accepted amount deposit
     * @param deposit2Min minimum accepted amount2 deposit
     * @param tfee trading fee in basis points
     * @return
     */
    std::pair<TER, STAmount>
    equalDepositTokens(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& amountBalance,
        STAmount const& amount2Balance,
        STAmount const& lptAMMBalance,
        STAmount const& lpTokensDeposit,
        std::optional<STAmount> const& depositMin,
        std::optional<STAmount> const& deposit2Min,
        std::uint16_t tfee);

    /** Equal asset deposit (Asset1In, Asset2In) with the constraint on
     * the maximum amount of both assets that the trader is willing to deposit.
     * The trading fee is not charged.
     * @param view
     * @param ammAccount
     * @param amountBalance current AMM asset1 balance
     * @param amount2Balance current AMM asset2 balance
     * @param lptAMMBalance current AMM LPT balance
     * @param amount maximum asset1 deposit amount
     * @param amount2 maximum asset2 deposit amount
     * @param lpTokensDepositMin minimum accepted LPTokens deposit
     * @param tfee trading fee in basis points
     * @return
     */
    std::pair<TER, STAmount>
    equalDepositLimit(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& amountBalance,
        STAmount const& amount2Balance,
        STAmount const& lptAMMBalance,
        STAmount const& amount,
        STAmount const& amount2,
        std::optional<STAmount> const& lpTokensDepositMin,
        std::uint16_t tfee);

    /** Single asset deposit (Asset1In) by the amount.
     * The trading fee is charged.
     * @param view
     * @param ammAccount
     * @param amountBalance current AMM asset1 balance
     * @param lptAMMBalance current AMM LPT balance
     * @param amount requested asset1 deposit amount
     * @param lpTokensDepositMin minimum accepted LPTokens deposit
     * @param tfee trading fee in basis points
     * @return
     */
    std::pair<TER, STAmount>
    singleDeposit(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& amountBalance,
        STAmount const& lptAMMBalance,
        STAmount const& amount,
        std::optional<STAmount> const& lpTokensDepositMin,
        std::uint16_t tfee);

    /** Single asset deposit (Asset1In, LPTokens) by the tokens.
     * The trading fee is charged.
     * @param view
     * @param ammAccount
     * @param amountBalance current AMM asset1 balance
     * @param amount max asset1 to deposit
     * @param lptAMMBalance current AMM LPT balance
     * @param lpTokensDeposit amount of tokens to deposit
     * @param tfee trading fee in basis points
     * @return
     */
    std::pair<TER, STAmount>
    singleDepositTokens(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& amountBalance,
        STAmount const& amount,
        STAmount const& lptAMMBalance,
        STAmount const& lpTokensDeposit,
        std::uint16_t tfee);

    /** Single asset deposit (Asset1In, EPrice) with two constraints.
     * The trading fee is charged.
     * @param view
     * @param ammAccount
     * @param amountBalance current AMM asset1 balance
     * @param amount requested asset1 deposit amount
     * @param lptAMMBalance current AMM LPT balance
     * @param ePrice maximum effective price
     * @param tfee
     * @return
     */
    std::pair<TER, STAmount>
    singleDepositEPrice(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& amountBalance,
        STAmount const& amount,
        STAmount const& lptAMMBalance,
        STAmount const& ePrice,
        std::uint16_t tfee);

    /** Equal deposit in empty AMM state (LP tokens balance is 0)
     * @param view
     * @param ammAccount
     * @param amount requested asset1 deposit amount
     * @param amount2 requested asset2 deposit amount
     * @param tfee
     * @return
     */
    std::pair<TER, STAmount>
    equalDepositInEmptyState(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& amount,
        STAmount const& amount2,
        Issue const& lptIssue,
        std::uint16_t tfee);
};

}  // namespace ripple

#endif  // RIPPLE_TX_AMMDEPOSIT_H_INCLUDED
