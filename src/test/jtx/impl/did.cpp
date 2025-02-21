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

#include <test/jtx/did.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {
namespace jtx {

/** DID operations. */
namespace did {

Json::Value
set(jtx::Account const& account)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::DIDSet;
    jv[jss::Account] = to_string(account.id());
    jv[jss::Flags] = tfUniversal;
    return jv;
}

Json::Value
setValid(jtx::Account const& account)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::DIDSet;
    jv[jss::Account] = to_string(account.id());
    jv[jss::Flags] = tfUniversal;
    jv[sfURI.jsonName] = strHex(std::string{"uri"});
    return jv;
}

Json::Value
del(jtx::Account const& account, std::optional<jtx::Account> const& onBehalfOf)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::DIDDelete;
    jv[jss::Account] = to_string(account.id());
    jv[jss::Flags] = tfUniversal;
    if (onBehalfOf)
        jv[sfOnBehalfOf.jsonName] = onBehalfOf->human();
    return jv;
}

bool
checkVL(Slice const& result, std::string expected)
{
    Serializer s;
    s.addRaw(result);
    return s.getString() == expected;
}
}  // namespace did

}  // namespace jtx

}  // namespace test
}  // namespace ripple
