//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#include <test/jtx/ledgerStateFix.h>

#include <xrpld/app/tx/detail/LedgerStateFix.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {
namespace jtx {

namespace ledgerStateFix {

// Fix NFTokenPage links on owner's account.  acct pays fee.
Json::Value
nftPageLinks(jtx::Account const& acct, jtx::Account const& owner)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = acct.human();
    jv[sfLedgerFixType.jsonName] = LedgerStateFix::nfTokenPageLink;
    jv[sfOwner.jsonName] = owner.human();
    jv[sfTransactionType.jsonName] = jss::LedgerStateFix;
    jv[sfFlags.jsonName] = tfUniversal;
    return jv;
}

}  // namespace ledgerStateFix

}  // namespace jtx
}  // namespace test
}  // namespace ripple
