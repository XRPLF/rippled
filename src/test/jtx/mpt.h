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

#ifndef RIPPLE_TEST_JTX_MPT_H_INCLUDED
#define RIPPLE_TEST_JTX_MPT_H_INCLUDED

#include <test/jtx.h>
#include <test/jtx/ter.h>
#include <test/jtx/txflags.h>

#include <xrpl/protocol/UintTypes.h>

namespace ripple {
namespace test {
namespace jtx {

namespace {
using AccountP = Account const*;
}

class MPTTester;

// Check flags settings on MPT create
class mptflags
{
private:
    MPTTester& tester_;
    std::uint32_t flags_;
    AccountP holder_;

public:
    mptflags(MPTTester& tester, std::uint32_t flags, AccountP holder = nullptr)
        : tester_(tester), flags_(flags), holder_(holder)
    {
    }

    void
    operator()(Env& env) const;
};

// Check mptissuance or mptoken amount balances on payment
class mptpay
{
private:
    MPTTester const& tester_;
    Account const& account_;
    std::int64_t const amount_;

public:
    mptpay(MPTTester& tester, Account const& account, std::int64_t amount)
        : tester_(tester), account_(account), amount_(amount)
    {
    }

    void
    operator()(Env& env) const;
};

class requireAny
{
private:
    std::function<bool()> cb_;

public:
    requireAny(std::function<bool()> const& cb) : cb_(cb)
    {
    }

    void
    operator()(Env& env) const;
};

struct MPTConstr
{
    std::vector<AccountP> holders = {};
    PrettyAmount const& xrp = XRP(10'000);
    PrettyAmount const& xrpHolders = XRP(10'000);
    bool fund = true;
    bool close = true;
};

struct MPTCreate
{
    std::optional<std::string> maxAmt = std::nullopt;
    std::optional<std::uint8_t> assetScale = std::nullopt;
    std::optional<std::uint16_t> transferFee = std::nullopt;
    std::optional<std::string> metadata = std::nullopt;
    std::optional<std::uint32_t> ownerCount = std::nullopt;
    std::optional<std::uint32_t> holderCount = std::nullopt;
    bool fund = true;
    std::optional<std::uint32_t> flags = {0};
    std::optional<TER> err = std::nullopt;
};

struct MPTDestroy
{
    AccountP issuer = nullptr;
    std::optional<MPTID> id = std::nullopt;
    std::optional<std::uint32_t> ownerCount = std::nullopt;
    std::optional<std::uint32_t> holderCount = std::nullopt;
    std::optional<std::uint32_t> flags = std::nullopt;
    std::optional<TER> err = std::nullopt;
};

struct MPTAuthorize
{
    AccountP account = nullptr;
    AccountP holder = nullptr;
    std::optional<MPTID> id = std::nullopt;
    std::optional<std::uint32_t> ownerCount = std::nullopt;
    std::optional<std::uint32_t> holderCount = std::nullopt;
    std::optional<std::uint32_t> flags = std::nullopt;
    std::optional<TER> err = std::nullopt;
};

struct MPTSet
{
    AccountP account = nullptr;
    AccountP holder = nullptr;
    std::optional<MPTID> id = std::nullopt;
    std::optional<std::uint32_t> ownerCount = std::nullopt;
    std::optional<std::uint32_t> holderCount = std::nullopt;
    std::optional<std::uint32_t> flags = std::nullopt;
    std::optional<TER> err = std::nullopt;
};

class MPTTester
{
    Env& env_;
    Account const& issuer_;
    std::unordered_map<std::string, AccountP> const holders_;
    std::optional<MPTID> id_;
    std::optional<uint256> issuanceKey_;
    bool close_;

public:
    MPTTester(Env& env, Account const& issuer, MPTConstr const& constr = {});

    void
    create(MPTCreate const& arg = MPTCreate{});

    void
    destroy(MPTDestroy const& arg = MPTDestroy{});

    void
    authorize(MPTAuthorize const& arg = MPTAuthorize{});

    void
    set(MPTSet const& set = {});

    [[nodiscard]] bool
    checkMPTokenAmount(Account const& holder, std::int64_t expectedAmount)
        const;

    [[nodiscard]] bool
    checkMPTokenOutstandingAmount(std::int64_t expectedAmount) const;

    [[nodiscard]] bool
    checkFlags(uint32_t const expectedFlags, AccountP holder = nullptr) const;

    Account const&
    issuer() const
    {
        return issuer_;
    }
    Account const&
    holder(std::string const& h) const;

    void
    pay(Account const& src,
        Account const& dest,
        std::int64_t amount,
        std::optional<TER> err = std::nullopt);

    void
    claw(
        Account const& issuer,
        Account const& holder,
        std::int64_t amount,
        std::optional<TER> err = std::nullopt);

    PrettyAmount
    mpt(std::int64_t amount) const;

    uint256 const&
    issuanceKey() const
    {
        assert(issuanceKey_);
        return *issuanceKey_;
    }

    MPTID const&
    issuanceID() const
    {
        assert(id_);
        return *id_;
    }

    std::int64_t
    getAmount(Account const& account) const;

private:
    using SLEP = std::shared_ptr<SLE const>;
    bool
    forObject(
        std::function<bool(SLEP const& sle)> const& cb,
        AccountP holder = nullptr) const;

    template <typename A>
    TER
    submit(A const& arg, Json::Value const& jv)
    {
        if (arg.err)
        {
            if (arg.flags && arg.flags > 0)
                env_(jv, txflags(*arg.flags), ter(*arg.err));
            else
                env_(jv, ter(*arg.err));
        }
        else if (arg.flags && arg.flags > 0)
            env_(jv, txflags(*arg.flags));
        else
            env_(jv);
        auto const err = env_.ter();
        if (close_)
            env_.close();
        if (arg.ownerCount)
            env_.require(owners(issuer_, *arg.ownerCount));
        if (arg.holderCount)
        {
            for (auto it : holders_)
                env_.require(owners(*it.second, *arg.holderCount));
        }
        return err;
    }

    std::unordered_map<std::string, AccountP>
    makeHolders(std::vector<AccountP> const& holders);

    std::uint32_t
    getFlags(AccountP holder) const;
};

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
