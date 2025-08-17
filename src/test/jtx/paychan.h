//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

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

#ifndef RIPPLE_TEST_JTX_PAYCHAN_H_INCLUDED
#define RIPPLE_TEST_JTX_PAYCHAN_H_INCLUDED

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/owners.h>
#include <test/jtx/rate.h>

#include <xrpl/protocol/Indexes.h>

namespace ripple {
namespace test {
namespace jtx {

/** Paychan operations. */
namespace paychan {

Json::Value
create(
    AccountID const& account,
    AccountID const& to,
    STAmount const& amount,
    NetClock::duration const& settleDelay,
    PublicKey const& pk,
    std::optional<NetClock::time_point> const& cancelAfter = std::nullopt,
    std::optional<std::uint32_t> const& dstTag = std::nullopt);

inline Json::Value
create(
    Account const& account,
    Account const& to,
    STAmount const& amount,
    NetClock::duration const& settleDelay,
    PublicKey const& pk,
    std::optional<NetClock::time_point> const& cancelAfter = std::nullopt,
    std::optional<std::uint32_t> const& dstTag = std::nullopt)
{
    return create(
        account.id(), to.id(), amount, settleDelay, pk, cancelAfter, dstTag);
}

Json::Value
fund(
    AccountID const& account,
    uint256 const& channel,
    STAmount const& amount,
    std::optional<NetClock::time_point> const& expiration = std::nullopt);

Json::Value
claim(
    AccountID const& account,
    uint256 const& channel,
    std::optional<STAmount> const& balance = std::nullopt,
    std::optional<STAmount> const& amount = std::nullopt,
    std::optional<Slice> const& signature = std::nullopt,
    std::optional<PublicKey> const& pk = std::nullopt);

uint256
channel(
    AccountID const& account,
    AccountID const& dst,
    std::uint32_t seqProxyValue);

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
rate(
    Env& env,
    Account const& account,
    Account const& dest,
    std::uint32_t const& seq);

}  // namespace paychan

}  // namespace jtx

}  // namespace test
}  // namespace ripple

#endif
