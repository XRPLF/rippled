#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/SignerUtils.h>

namespace xrpl {
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
set_reserve(jtx::Account const& account, std::uint32_t flags, std::uint32_t reserveCount);

Json::Value
set_max_fee(jtx::Account const& account, std::uint32_t flags, STAmount maxFee);

Json::Value
del(jtx::Account const& account);

Json::Value
transfer(
    jtx::Account const& account,
    uint32_t flags,
    std::optional<uint256> const& index = std::nullopt);

struct counterpartySponsor
{
private:
    jtx::Account sponsor_;

public:
    counterpartySponsor(jtx::Account const& account) : sponsor_(account)
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
    as(jtx::Account const& account, std::uint32_t flags = 0) : sponsor_(account), flags(flags)
    {
    }

    void
    operator()(jtx::Env&, jtx::JTx& jtx) const;
};

Json::Value
ledgerEntry(jtx::Env& env, jtx::Account const& sponsor, jtx::Account const& sponsee);

}  // namespace sponsor
}  // namespace jtx
}  // namespace test
}  // namespace xrpl

#endif
