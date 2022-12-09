//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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
#ifndef RIPPLE_TEST_JTX_AMMTEST_H_INCLUDED
#define RIPPLE_TEST_JTX_AMMTEST_H_INCLUDED

#include <ripple/beast/unit_test/suite.hpp>
#include <ripple/protocol/Feature.h>
#include <test/jtx/Account.h>
#include <test/jtx/amount.h>
#include <test/jtx/ter.h>

namespace ripple {
namespace test {
namespace jtx {

enum class Fund { All, Acct, Gw, None };

void
fund(
    jtx::Env& env,
    jtx::Account const& gw,
    std::vector<jtx::Account> const& accounts,
    std::vector<STAmount> const& amts,
    Fund how);

void
fund(
    jtx::Env& env,
    jtx::Account const& gw,
    std::vector<jtx::Account> const& accounts,
    STAmount const& xrp,
    std::vector<STAmount> const& amts = {},
    Fund how = Fund::All);

void
fund(
    jtx::Env& env,
    std::vector<jtx::Account> const& accounts,
    STAmount const& xrp,
    std::vector<STAmount> const& amts = {},
    Fund how = Fund::All);

class AMMTest : public beast::unit_test::suite
{
protected:
    jtx::Account const gw;
    jtx::Account const carol;
    jtx::Account const alice;
    jtx::Account const bob;
    jtx::IOU const USD;
    jtx::IOU const EUR;
    jtx::IOU const GBP;
    jtx::IOU const BTC;
    jtx::IOU const BAD;

public:
    AMMTest();

protected:
    /** testAMM() funds 30,000XRP and 30,000IOU
     * for each non-XRP asset to Alice and Carol
     */
    template <typename F>
    void
    testAMM(
        F&& cb,
        std::optional<std::pair<STAmount, STAmount>> const& pool = std::nullopt,
        std::uint32_t fee = 0,
        std::optional<jtx::ter> const& ter = std::nullopt,
        std::optional<FeatureBitset> const& features = std::nullopt)
    {
        using namespace jtx;
        auto env = features ? Env{*this, *features} : Env{*this};

        auto const [asset1, asset2] =
            pool ? *pool : std::make_pair(XRP(10000), USD(10000));
        auto tofund = [&](STAmount const& a) -> STAmount {
            if (a.native())
                return XRP(30000);
            return STAmount{a.issue(), 30000};
        };
        auto const toFund1 = tofund(asset1);
        auto const toFund2 = tofund(asset2);
        assert(asset1 <= toFund1 && asset2 <= toFund2);

        if (!asset1.native() && !asset2.native())
            fund(env, gw, {alice, carol}, {toFund1, toFund2}, Fund::All);
        else if (asset1.native())
            fund(env, gw, {alice, carol}, {toFund2}, Fund::All);
        else if (asset2.native())
            fund(env, gw, {alice, carol}, {toFund1}, Fund::All);

        AMM ammAlice(env, alice, asset1, asset2, false, fee);
        BEAST_EXPECT(
            ammAlice.expectBalances(asset1, asset2, ammAlice.tokens()));
        cb(ammAlice, env);
    }
};

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif  // RIPPLE_TEST_JTX_AMMTEST_H_INCLUDED
