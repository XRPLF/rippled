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

#ifndef RIPPLE_TEST_JTX_CFT_H_INCLUDED
#define RIPPLE_TEST_JTX_CFT_H_INCLUDED

#include <test/jtx/Account.h>

#include <ripple/protocol/UintTypes.h>

namespace ripple {
namespace test {
namespace jtx {

namespace cft {

/** Issue a CFT with default fields. */
Json::Value
create(jtx::Account const& account);

/** Issue a CFT with user-defined fields. */
Json::Value
create(
    jtx::Account const& account,
    std::uint32_t const maxAmt,
    std::uint8_t const assetScale,
    std::uint16_t transferFee,
    std::string metadata);

/** Destroy a CFT. */
Json::Value
destroy(Account const& account, uint192 const& id);

/** Authorize a CFT. */
Json::Value
authorize(
    jtx::Account const& account,
    uint192 const& issuanceID,
    std::optional<jtx::Account> const& holder);

/** Set a CFT. */
Json::Value
set(jtx::Account const& account,
    uint192 const& issuanceID,
    std::optional<jtx::Account> const& holder);
}  // namespace cft

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
