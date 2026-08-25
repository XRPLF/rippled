#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>

#include <cstdint>
#include <optional>
#include <utility>

namespace xrpl::test::jtx::sponsor {

json::Value
set(jtx::Account const& account,
    std::uint32_t flags,
    std::optional<std::uint32_t> const reserveCount = std::nullopt,
    std::optional<STAmount> const feeAmount = std::nullopt,
    std::optional<STAmount> const maxFee = std::nullopt);

inline json::Value
set_fee(
    jtx::Account const& account,
    std::uint32_t flags,
    STAmount feeAmount,
    std::optional<STAmount> maxFee = std::nullopt)
{
    return set(account, flags, std::nullopt, std::move(feeAmount), std::move(maxFee));
}

inline json::Value
set_reserve(jtx::Account const& account, std::uint32_t flags, std::uint32_t reserveCount)
{
    return set(account, flags, reserveCount);
}

inline json::Value
set_max_fee(jtx::Account const& account, std::uint32_t flags, STAmount maxFee)
{
    return set(account, flags, std::nullopt, std::nullopt, std::move(maxFee));
}

json::Value
del(jtx::Account const& account);

json::Value
transfer(
    jtx::Account const& account,
    uint32_t flags,
    std::optional<uint256> const& index = std::nullopt);

struct CounterpartySponsor
{
private:
    jtx::Account sponsor_;

public:
    CounterpartySponsor(jtx::Account account) : sponsor_(std::move(account))
    {
    }

    void
    operator()(jtx::Env&, jtx::JTx& jtx) const;
};

struct SponseeAcc
{
private:
    jtx::Account sponsee_;

public:
    SponseeAcc(jtx::Account account) : sponsee_(std::move(account))
    {
    }

    void
    operator()(jtx::Env&, jtx::JTx& jtx) const;
};

struct As
{
private:
    jtx::Account sponsor_;
    std::uint32_t flags_;

public:
    As(jtx::Account account, std::uint32_t flags = 0) : sponsor_(std::move(account)), flags_(flags)
    {
    }

    void
    operator()(jtx::Env&, jtx::JTx& jtx) const;
};

json::Value
ledgerEntry(jtx::Env& env, jtx::Account const& sponsor, jtx::Account const& sponsee);

STAmount
sponsorshipFeeBalance(jtx::Env& env, jtx::Account const& sponsor, jtx::Account const& sponsee);

}  // namespace xrpl::test::jtx::sponsor
