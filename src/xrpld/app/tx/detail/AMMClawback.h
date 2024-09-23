//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2024 Ripple Labs Inc.

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

#ifndef RIPPLE_TX_AMMCLAWBACK_H_INCLUDED
#define RIPPLE_TX_AMMCLAWBACK_H_INCLUDED

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
     * @param view
     * @param ammAccount current AMM account
     * @param amountBalance current AMM asset1 balance
     * @param amount2Balance current AMM asset2 balance
     * @param lptAMMBalance current AMM LPT balance
     * @param amount asset1 withdraw amount
     * @param tfee trading fee in basis points
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
        STAmount const& amount,
        std::uint16_t tfee);
};

}  // namespace ripple

#endif
