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
     * @param asset1Withdraw withdraw amount
     * @param asset2Withdraw withdraw amount
     * @param lptAMMBalance AMM LPT balance
     * @param lpTokensWithdraw LPT withdraw amount
     * @return
     */
    std::pair<TER, STAmount>
    withdraw(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& asset1Withdraw,
        std::optional<STAmount> const& asset2Withdraw,
        STAmount const& lpAMMBalance,
        STAmount const& lpTokensWithdraw);

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
     * @param lpTokensWithdraw amount of tokens to withdraw
     * @return
     */
    std::pair<TER, STAmount>
    equalWithdrawalTokens(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& asset1Balance,
        STAmount const& asset2Balance,
        STAmount const& lptAMMBalance,
        STAmount const& lpTokensWithdraw);

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
    std::pair<TER, STAmount>
    equalWithdrawalLimit(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& asset1Balance,
        STAmount const& asset2Balance,
        STAmount const& lptAMMBalance,
        STAmount const& asset1Out,
        STAmount const& asset2Out);

    /** Single asset withdrawal equivalent to the amount specified
     * in Asset1Out. The fee is charged.
     * @param ctx
     * @param view
     * @param ammAccount AMM account
     * @param asset1Balance current AMM asset1 balance
     * @param lptAMMBalance current AMM LPT balance
     * @param asset1Out asset1 withdraw amount
     * @param tfee trading fee in basis points
     * @return
     */
    std::pair<TER, STAmount>
    singleWithdrawal(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& asset1Balance,
        STAmount const& lptAMMBalance,
        STAmount const& asset1Out,
        std::uint16_t tfee);

    /** Single asset withdrawal proportional to the share
     * specified by tokens. The fee is charged.
     * @param view
     * @param ammAccount AMM account
     * @param asset1Balance current AMM asset1 balance
     * @param lptAMMBalance current AMM LPT balance
     * @param asset1Out asset1 withdraw amount
     * @param lpTokensWithdraw amount of tokens to withdraw
     * @param tfee trading fee in basis points
     * @return
     */
    std::pair<TER, STAmount>
    singleWithdrawalTokens(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& asset1Balance,
        STAmount const& lptAMMBalance,
        STAmount const& asset1Out,
        STAmount const& lpTokensWithdraw,
        std::uint16_t tfee);

    /**
     * @param view
     * @param ammAccount AMM account
     * @param asset1Balance current AMM asset1 balance
     * @param lptAMMBalance current AMM LPT balance
     * @param asset1Out asset1 withdraw amount
     * @param ePrice maximum asset1 effective price
     * @param tfee trading fee in basis points
     * @return
     */
    std::pair<TER, STAmount>
    singleWithdrawalEPrice(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& asset1Balance,
        STAmount const& lptAMMBalance,
        STAmount const& asset1Out,
        STAmount const& ePrice,
        std::uint16_t tfee);

    /** Delete AMM account.
     * @param view
     * @param ammAccountID
     * @return
     */
    TER
    deleteAccount(Sandbox& view, AccountID const& ammAccountID);

    /** Get transaction's LP Tokens. If 0 then
     * return all LP Tokens of the LP.
     */
    static std::optional<STAmount>
    getTxLPTokens(
        ReadView const& view,
        AccountID const& ammAccount,
        STTx const& tx,
        beast::Journal const journal);
};

}  // namespace ripple

#endif  // RIPPLE_TX_AMMWITHDRAW_H_INCLUDED
