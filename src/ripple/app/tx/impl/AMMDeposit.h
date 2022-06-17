//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2022 Ripple Labs Inc.

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

class AMMDeposit : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Custom};

    explicit AMMDeposit(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static TxConsequences
    makeTxConsequences(PreflightContext const& ctx);

    /** Enforce constraints beyond those of the Transactor base class. */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Enforce constraints beyond those of the Transactor base class. */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Gather information beyond what the Transactor base class gathers. */
    void
    preCompute() override;

    /** Attempt to create the AMM instance. */
    TER
    doApply() override;

private:
    std::pair<TER, bool>
    applyGuts(Sandbox& view);

    /** Deposit requested assets and tokens amount into LP account.
     * @param view
     * @param ammAccount AMM account
     * @param asset1 deposit amount
     * @param asset2 deposit amount
     * @param lpTokens LPT deposit amount
     * @return
     */
    TER
    deposit(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& asset1,
        std::optional<STAmount> const& asset2,
        STAmount const& lpTokens);

    /** Equal asset deposit for the specified share of the AMM instance pools.
     * Depositing assets proportionally doesn't change the assets ratio and
     * consequently doesn't change the relative pricing. Therefore the fee
     * is not charged.
     * @param view
     * @param ammAccount AMM account
     * @param asset1Balance current AMM asset1 balance
     * @param asset2Balance current AMM asset2 balance
     * @param lptAMMBalance current AMM LPT balance
     * @param tokensPct percentage of the AMM instance pool in basis points
     * @return
     */
    TER
    equalDepositTokens(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& asset1Balance,
        STAmount const& asset2Balance,
        STAmount const& lptAMMBalance,
        STAmount const& tokens);

    /** Equal asset deposit with the constraint on the maximum amount of
     * both assets that the trader is willing to deposit. The fee is not
     * charged.
     * @param view
     * @param ammAccount AMM account
     * @param asset1Balance current AMM asset1 balance
     * @param asset2Balance current AMM asset2 balance
     * @param lptAMMBalance current AMM LPT balance
     * @param asset1In maximum asset1 deposit amount
     * @param asset2In maximum asset2 deposit amount
     * @return
     */
    TER
    equalDepositLimit(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& asset1Balance,
        STAmount const& asset2Balance,
        STAmount const& lptAMMBalance,
        STAmount const& asset1In,
        STAmount const& asset2In);

    /** Single asset deposit by the amount. The fee is charged.
     * @param view
     * @param ammAccount AMM account
     * @param asset1Balance current AMM asset1 balance
     * @param lptAMMBalance current AMM LPT balance
     * @param asset1In requested asset1 deposit amount
     * @param tfee trading fee in basis points
     * @return
     */
    TER
    singleDeposit(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& asset1Balance,
        STAmount const& lptAMMBalance,
        STAmount const& asset1In,
        std::uint16_t tfee);

    /** Single asset deposit by the tokens. The trading fee is charged.
     * The pool to deposit into is determined in the applyGuts() via
     * asset1InDetails issue. The fee is charged.
     * @param view
     * @param ammAccount AMM account
     * @param asset1Balance current AMM asset1 balance
     * @param lptAMMBalance current AMM LPT balance
     * @param tokens number of tokens to deposit
     * @param tfee trading fee in basis points
     * @return
     */
    TER
    singleDepositTokens(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& asset1Balance,
        STAmount const& lptAMMBalance,
        STAmount const& tokens,
        std::uint16_t tfee);

    /** Single asset deposit with the constraint that the effective price
     * of the trade doesn't exceed the specified. The fee is charged.
     * @param view
     * @param ammAccount AMM account
     * @param asset1Balance current AMM asset1 balance
     * @param asset1In requested asset1 deposit amount
     * @param lptAMMBalance current AMM LPT balance
     * @param ePrice maximum effective price
     * @param tfee
     * @return
     */
    TER
    singleDepositEPrice(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& asset1Balance,
        STAmount const& asset1In,
        STAmount const& lptAMMBalance,
        STAmount const& ePrice,
        std::uint16_t tfee);
};

}  // namespace ripple

#endif  // RIPPLE_TX_AMMDEPOSIT_H_INCLUDED
