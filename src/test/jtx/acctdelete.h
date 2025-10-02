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

#ifndef XRPL_TEST_JTX_ACCTDELETE_H_INCLUDED
#define XRPL_TEST_JTX_ACCTDELETE_H_INCLUDED

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>

#include <xrpl/beast/unit_test/suite.h>

namespace ripple {
namespace test {
namespace jtx {

/** Delete account.  If successful transfer remaining XRP to dest. */
Json::Value
acctdelete(Account const& account, Account const& dest);

// Close the ledger until the ledger sequence is large enough to close
// the account.  If margin is specified, close the ledger so `margin`
// more closes are needed
void
incLgrSeqForAccDel(
    jtx::Env& env,
    jtx::Account const& acc,
    std::uint32_t margin = 0);

}  // namespace jtx

}  // namespace test
}  // namespace ripple

#endif
