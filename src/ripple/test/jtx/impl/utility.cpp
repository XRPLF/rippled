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

#include <BeastConfig.h>
#include <ripple/test/jtx/utility.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/STParsedJSON.h>
#include <cstring>

namespace ripple {
namespace test {
namespace jtx {

STObject
parse (Json::Value const& jv)
{
    STParsedJSONObject p("tx_json", jv);
    if (! p.object)
        throw parse_error(
            rpcErrorString(p.error));
    return std::move(*p.object);
}

void
sign (Json::Value& jv,
    Account const& account)
{
    jv[jss::SigningPubKey] =
        strHex(make_Slice(
            account.pk().getAccountPublic()));
    Serializer ss;
    ss.add32 (HashPrefix::txSign);
    parse(jv).add(ss);
    jv[jss::TxnSignature] = strHex(make_Slice(
        account.sk().accountPrivateSign(
            ss.getData())));
}

void
fill_fee (Json::Value& jv,
    Ledger const& ledger)
{
    if (jv.isMember(jss::Fee))
        return;
    jv[jss::Fee] = std::to_string(
        ledger.getBaseFee());
}

void
fill_seq (Json::Value& jv,
    Ledger const& ledger)
{
    if (jv.isMember(jss::Sequence))
        return;
    // VFALCO TODO Use
    //   parseBase58<AccountID>(jv[jss::Account].asString())
    RippleAddress ra;
    ra.setAccountID(jv[jss::Account].asString());
    auto const ar = ledger.read(
        keylet::account(ra.getAccountID()));
    if (!ar)
        return;
    jv[jss::Sequence] =
        ar->getFieldU32(sfSequence);
}

} // jtx
} // test
} // ripple
