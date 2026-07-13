#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SecretKey.h>

#include <cstdint>
#include <optional>

/**
 * Paychan operations.
 */
namespace xrpl::test::jtx::paychan {

json::Value
create(
    AccountID const& account,
    AccountID const& to,
    STAmount const& amount,
    NetClock::duration const& settleDelay,
    PublicKey const& pk,
    std::optional<NetClock::time_point> const& cancelAfter = std::nullopt,
    std::optional<std::uint32_t> const& dstTag = std::nullopt);

inline json::Value
create(
    Account const& account,
    Account const& to,
    STAmount const& amount,
    NetClock::duration const& settleDelay,
    PublicKey const& pk,
    std::optional<NetClock::time_point> const& cancelAfter = std::nullopt,
    std::optional<std::uint32_t> const& dstTag = std::nullopt)
{
    return create(account.id(), to.id(), amount, settleDelay, pk, cancelAfter, dstTag);
}

json::Value
fund(
    AccountID const& account,
    uint256 const& channel,
    STAmount const& amount,
    std::optional<NetClock::time_point> const& expiration = std::nullopt);

json::Value
claim(
    AccountID const& account,
    uint256 const& channel,
    std::optional<STAmount> const& balance = std::nullopt,
    std::optional<STAmount> const& amount = std::nullopt,
    std::optional<Slice> const& signature = std::nullopt,
    std::optional<PublicKey> const& pk = std::nullopt);

uint256
channel(AccountID const& account, AccountID const& dst, std::uint32_t seqProxyValue);

inline uint256
channel(Account const& account, Account const& dst, std::uint32_t seqProxyValue)
{
    return channel(account.id(), dst.id(), seqProxyValue);
}

STAmount
channelBalance(ReadView const& view, uint256 const& chan);

STAmount
channelAmount(ReadView const& view, uint256 const& chan);

bool
channelExists(ReadView const& view, uint256 const& chan);

Buffer
signClaimAuth(
    PublicKey const& pk,
    SecretKey const& sk,
    uint256 const& channel,
    STAmount const& authAmt);

Rate
rate(Env& env, Account const& account, Account const& dest, std::uint32_t const& seq);

}  // namespace xrpl::test::jtx::paychan
