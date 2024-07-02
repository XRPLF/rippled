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
#ifndef RIPPLE_TEST_JTX_AMMTEST_H_INCLUDED
#define RIPPLE_TEST_JTX_AMMTEST_H_INCLUDED

#include <ripple/beast/unit_test/suite.h>
#include <ripple/protocol/Feature.h>
#include <test/jtx/Account.h>
#include <test/jtx/amount.h>
#include <test/jtx/ter.h>

namespace ripple {
namespace test {
namespace jtx {

class AMM;

enum class Fund { All, Acct, Gw, IOUOnly };

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

class AMMTestBase : public beast::unit_test::suite
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
    AMMTestBase();

protected:
    /** testAMM() funds 30,000XRP and 30,000IOU
     * for each non-XRP asset to Alice and Carol
     */
    void
    testAMM(
        std::function<void(jtx::AMM&, jtx::Env&)>&& cb,
        std::optional<std::pair<STAmount, STAmount>> const& pool = std::nullopt,
        std::uint16_t tfee = 0,
        std::optional<jtx::ter> const& ter = std::nullopt,
        std::vector<FeatureBitset> const& features = {supported_amendments()});
};

class AMMTest : public jtx::AMMTestBase
{
protected:
    XRPAmount
    reserve(jtx::Env& env, std::uint32_t count) const;

    XRPAmount
    ammCrtFee(jtx::Env& env) const;

    /* Path_test */
    /************************************************/
    class gate
    {
    private:
        std::condition_variable cv_;
        std::mutex mutex_;
        bool signaled_ = false;

    public:
        // Thread safe, blocks until signaled or period expires.
        // Returns `true` if signaled.
        template <class Rep, class Period>
        bool
        wait_for(std::chrono::duration<Rep, Period> const& rel_time)
        {
            std::unique_lock<std::mutex> lk(mutex_);
            auto b = cv_.wait_for(lk, rel_time, [this] { return signaled_; });
            signaled_ = false;
            return b;
        }

        void
        signal()
        {
            std::lock_guard lk(mutex_);
            signaled_ = true;
            cv_.notify_all();
        }
    };

    jtx::Env
    pathTestEnv();

    Json::Value
    find_paths_request(
        jtx::Env& env,
        jtx::Account const& src,
        jtx::Account const& dst,
        STAmount const& saDstAmount,
        std::optional<STAmount> const& saSendMax = std::nullopt,
        std::optional<Currency> const& saSrcCurrency = std::nullopt);

    std::tuple<STPathSet, STAmount, STAmount>
    find_paths(
        jtx::Env& env,
        jtx::Account const& src,
        jtx::Account const& dst,
        STAmount const& saDstAmount,
        std::optional<STAmount> const& saSendMax = std::nullopt,
        std::optional<Currency> const& saSrcCurrency = std::nullopt);
};

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif  // RIPPLE_TEST_JTX_AMMTEST_H_INCLUDED
