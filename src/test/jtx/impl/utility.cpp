//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <test/jtx/utility.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/jss.h>
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/protocol/UintTypes.h>
#include <ripple/basics/contract.h>
#include <cstring>

namespace ripple {
namespace test {
namespace jtx {

STObject
parse (Json::Value const& jv)
{
    STParsedJSONObject p("tx_json", jv);
    if (! p.object)
        Throw<parse_error> (
            rpcErrorString(p.error));
    return std::move(*p.object);
}

void
sign (Json::Value& jv,
    Account const& account)
{
    jv[jss::SigningPubKey] =
        strHex(account.pk().slice());
    Serializer ss;
    ss.add32 (HashPrefix::txSign);
    parse(jv).addWithoutSigningFields(ss);
    auto const sig = ripple::sign(
        account.pk(), account.sk(), ss.slice());
    jv[jss::TxnSignature] =
        strHex(Slice{ sig.data(), sig.size() });
}

void
fill_fee (Json::Value& jv,
    ReadView const& view)
{
    if (jv.isMember(jss::Fee))
        return;
    jv[jss::Fee] = to_string(
        view.fees().base);
}

void
fill_seq (Json::Value& jv,
    ReadView const& view)
{
    if (jv.isMember(jss::Sequence))
        return;
    auto const account =
        parseBase58<AccountID>(
            jv[jss::Account].asString());
    if (! account)
        Throw<parse_error> (
            "unexpected invalid Account");
    auto const ar = view.read(
        keylet::account(*account));
    if (! ar)
        Throw<parse_error> (
            "unexpected missing account root");
    jv[jss::Sequence] =
        ar->getFieldU32(sfSequence);
}

} // jtx
} // test
} // ripple
