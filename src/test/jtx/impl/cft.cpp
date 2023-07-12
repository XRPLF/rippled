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

#include <test/jtx/cft.h>

#include <ripple/protocol/jss.h>

namespace ripple {
namespace test {
namespace jtx {

namespace cft {

Json::Value
create(jtx::Account const& account, std::string const& asset)
{
    auto const assetCurrency = to_currency(asset);
    assert(assetCurrency != noCurrency());

    Json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfAssetCode.jsonName] = ripple::to_string(assetCurrency);
    jv[sfTransactionType.jsonName] = jss::CFTokenIssuanceCreate;
    return jv;
}

Json::Value
destroy(jtx::Account const& account, std::string const& id)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfCFTokenIssuanceID.jsonName] = id;
    jv[sfTransactionType.jsonName] = jss::CFTokenIssuanceDestroy;
    return jv;
}

}  // namespace cft

}  // namespace jtx
}  // namespace test
}  // namespace ripple
