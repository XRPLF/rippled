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

#ifndef RIPPLE_TEST_JTX_CHECK_H_INCLUDED
#define RIPPLE_TEST_JTX_CHECK_H_INCLUDED

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/owners.h>

namespace ripple {
namespace test {
namespace jtx {

/** Check operations. */
namespace check {

/** Set Expiration on a JTx. */
class expiration
{
private:
    std::uint32_t const expry_;

public:
    explicit expiration(NetClock::time_point const& expiry)
        : expry_{expiry.time_since_epoch().count()}
    {
    }

    void
    operator()(Env&, JTx& jt) const
    {
        jt[sfExpiration.jsonName] = expry_;
    }
};

/** Set SourceTag on a JTx. */
class source_tag
{
private:
    std::uint32_t const tag_;

public:
    explicit source_tag(std::uint32_t tag) : tag_{tag}
    {
    }

    void
    operator()(Env&, JTx& jt) const
    {
        jt[sfSourceTag.jsonName] = tag_;
    }
};

/** Set DestinationTag on a JTx. */
class dest_tag
{
private:
    std::uint32_t const tag_;

public:
    explicit dest_tag(std::uint32_t tag) : tag_{tag}
    {
    }

    void
    operator()(Env&, JTx& jt) const
    {
        jt[sfDestinationTag.jsonName] = tag_;
    }
};

/** Cash a check requiring that a specific amount be delivered. */
Json::Value
cash(
    jtx::Account const& dest,
    uint256 const& checkId,
    STAmount const& amount,
    std::optional<jtx::Account> const& onBehalfOf = std::nullopt);

/** Type used to specify DeliverMin for cashing a check. */
struct DeliverMin
{
    STAmount value;
    explicit DeliverMin(STAmount const& deliverMin) : value(deliverMin)
    {
    }
};

/** Cash a check requiring that at least a minimum amount be delivered. */
Json::Value
cash(
    jtx::Account const& dest,
    uint256 const& checkId,
    DeliverMin const& atLeast,
    std::optional<jtx::Account> const& onBehalfOf = std::nullopt);

/** Cancel a check. */
Json::Value
cancel(
    jtx::Account const& dest,
    uint256 const& checkId,
    std::optional<jtx::Account> const& onBehalfOf = std::nullopt);

std::vector<std::shared_ptr<SLE const>>
checksOnAccount(test::jtx::Env& env, test::jtx::Account account);

}  // namespace check

/** Match the number of checks on the account. */
using checks = owner_count<ltCHECK>;

}  // namespace jtx

}  // namespace test
}  // namespace ripple

#endif
