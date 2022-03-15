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

#ifndef RIPPLE_TX_AMMWITHDRAW_H_INCLUDED
#define RIPPLE_TX_AMMWITHDRAW_H_INCLUDED

#include <ripple/app/tx/impl/Transactor.h>

namespace ripple {

class Sandbox;

class AMMWithdraw : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Custom};

    explicit AMMWithdraw(ApplyContext& ctx) : Transactor(ctx)
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

    /** Withdraw requested assets and tokens from AMM into LP account.
     * @param view
     * @param ammAccount AMM account
     * @param asset1 withdraw amount
     * @param asset2 withdraw amount
     * @param lptAMMBalance AMM LPT balance
     * @param lpTokens LPT withdraw amount
     * @return
     */
    TER
    withdraw(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& asset1,
        std::optional<STAmount> const& asset2,
        STAmount const& lpAMMBalance,
        STAmount const& lpTokens);

    /** Equal-asset withdrawal of some AMM instance pools
     * shares represented by the number of LPTokens .
     * Withdrawing assets proportionally does not change the assets ratio
     * and consequently does not change the relative pricing. Therefore the fee
     * is not charged.
     * @param view
     * @param ammAccount AMM account
     * @param asset1Balance current LP asset1 balance
     * @param asset2Balance current LP asset2 balance
     * @param lptAMMBalance current AMM LPT balance
     * @param tokens tokens to withdraw
     * @return
     */
    TER
    equalWithdrawalTokens(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& asset1Balance,
        STAmount const& asset2Balance,
        STAmount const& lptAMMBalance,
        STAmount const& tokens);

    /**
     * @param view
     * @param ammAccount AMM account
     * @param asset1Balance current AMM asset1 balance
     * @param asset2Balance current AMM asset2 balance
     * @param lptAMMBalance current AMM LPT balance
     * @param asset1Out asset1 withdraw amount
     * @param asset2Out max asset2 withdraw amount
     * @return
     */
    TER
    equalWithdrawalLimit(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& asset1Balance,
        STAmount const& asset2Balance,
        STAmount const& lptAMMBalance,
        STAmount const& asset1Out,
        STAmount const& asset2Out);

    /** Single asset withdrawal equivalent to the amount specified
     * in Asset1OutDetails. The fee is charged.
     * @param ctx
     * @param view
     * @param ammAccount AMM account
     * @param asset1Balance current AMM asset1 balance
     * @param lptAMMBalance current AMM LPT balance
     * @param asset1OutDetails asset1 withdraw amount
     * @param weight asset1 weight
     * @param tfee trading fee in basis points
     * @return
     */
    TER
    singleWithdrawal(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& asset1Balance,
        STAmount const& lptAMMBalance,
        STAmount const& asset1OutDetails,
        std::uint8_t weight,
        std::uint16_t tfee);

    /** Single asset withdrawal proportional to the share
     * specified by tokens. The fee is charged.
     * @param view
     * @param ammAccount AMM account
     * @param asset1Balance current AMM asset1 balance
     * @param lptAMMBalance current AMM LPT balance
     * @param tokens to withdraw
     * @param weight asset1 weight
     * @param tfee trading fee in basis points
     * @return
     */
    TER
    singleWithdrawalTokens(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& asset1Balance,
        STAmount const& lptAMMBalance,
        STAmount const& tokens,
        std::uint8_t weight,
        std::uint16_t tfee);

    /**
     * @param view
     * @param ammAccount AMM account
     * @param asset1Balance current AMM asset1 balance
     * @param asset2Balance current AMM asset2 balance
     * @param lptAMMBalance current AMM LPT balance
     * @param asset1OutDetails
     * @param maxSP
     * @param weight1 asset1 weight
     * @param tfee trading fee in basis points
     * @return
     */
    TER
    singleWithdrawMaxSP(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& asset1Balance,
        STAmount const& asset2Balance,
        STAmount const& lptAMMBalance,
        STAmount const& asset1OutDetails,
        STAmount const& maxSP,
        std::uint8_t weight1,
        std::uint16_t tfee);
};

}  // namespace ripple

#endif  // RIPPLE_TX_AMMWITHDRAW_H_INCLUDED
