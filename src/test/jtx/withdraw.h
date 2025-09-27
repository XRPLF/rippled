//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.

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

#ifndef RIPPLE_TEST_JTX_WITHDRAW_H_INCLUDED
#define RIPPLE_TEST_JTX_WITHDRAW_H_INCLUDED

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>

namespace ripple {
namespace test {
namespace jtx {

/** Withdraw preauthorize operations */
namespace withdraw {

/** Preauthorize for withdraw.  Invoke as withdraw::auth. */
Json::Value
auth(
    Account const& account,
    Account const& auth,
    uint256 const& firewallID,
    uint32_t seq,
    STAmount const& fee);

/** Remove preauthorization for withdraw.  Invoke as withdraw::unauth. */
Json::Value
unauth(
    Account const& account,
    Account const& unauth,
    uint256 const& firewallID,
    uint32_t seq,
    STAmount const& fee);

}  // namespace withdraw

}  // namespace jtx

}  // namespace test
}  // namespace ripple

#endif
