//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/SignerUtils.h>

namespace ripple {
namespace test {
namespace jtx {

namespace sponsor {

Json::Value
set(jtx::Account const& account,
    std::uint32_t flags,
    std::optional<std::uint32_t> reserveCount = std::nullopt,
    std::optional<STAmount> feeAmount = std::nullopt,
    std::optional<STAmount> maxFee = std::nullopt);

Json::Value
set_fee(
    jtx::Account const& account,
    std::uint32_t flags,
    STAmount feeAmount,
    std::optional<STAmount> maxFee = std::nullopt);

Json::Value
set_reserve(
    jtx::Account const& account,
    std::uint32_t flags,
    std::uint32_t reserveCount);

Json::Value
del(jtx::Account const& account);

Json::Value
transfer(
    jtx::Account const& account,
    std::optional<uint256> const& index = std::nullopt);

struct sponsorAcc
{
private:
    jtx::Account sponsor_;

public:
    sponsorAcc(jtx::Account const& account) : sponsor_(account)
    {
    }

    void
    operator()(jtx::Env&, jtx::JTx& jtx) const;
};

struct sponseeAcc
{
private:
    jtx::Account sponsee_;

public:
    sponseeAcc(jtx::Account const& account) : sponsee_(account)
    {
    }

    void
    operator()(jtx::Env&, jtx::JTx& jtx) const;
};

struct as
{
private:
    jtx::Account sponsor_;
    std::uint32_t flags;

public:
    as(jtx::Account const& account, std::uint32_t flags = 0)
        : sponsor_(account), flags(flags)
    {
    }

    void
    operator()(jtx::Env&, jtx::JTx& jtx) const;
};

struct sig
{
private:
    Reg signer_;

public:
    sig(Reg signer) : signer_(std::move(signer))
    {
    }

    void
    operator()(jtx::Env&, jtx::JTx& jtx) const;
};

struct msig
{
private:
    std::vector<Reg> signers;

public:
    msig(std::vector<Reg> signers_) : signers(std::move(signers_))
    {
        sortSigners(signers);
    }

    void
    operator()(jtx::Env&, jtx::JTx& jtx) const;
};

Json::Value
ledgerEntry(
    jtx::Env& env,
    jtx::Account const& sponsor,
    jtx::Account const& sponsee);

}  // namespace sponsor
}  // namespace jtx
}  // namespace test
}  // namespace ripple
