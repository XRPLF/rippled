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
create(jtx::Account const& account)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfTransactionType.jsonName] = jss::CFTokenIssuanceCreate;
    return jv;
}

Json::Value
create(
    jtx::Account const& account,
    std::uint32_t const maxAmt,
    std::uint8_t const assetScale,
    std::uint16_t transferFee,
    std::string metadata)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfTransactionType.jsonName] = jss::CFTokenIssuanceCreate;
    jv[sfMaximumAmount.jsonName] = maxAmt;
    jv[sfAssetScale.jsonName] = assetScale;
    jv[sfTransferFee.jsonName] = transferFee;
    jv[sfCFTokenMetadata.jsonName] = strHex(metadata);
    return jv;
}

Json::Value
destroy(jtx::Account const& account, ripple::uint256 const& id)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfCFTokenIssuanceID.jsonName] = to_string(id);
    jv[sfTransactionType.jsonName] = jss::CFTokenIssuanceDestroy;
    return jv;
}

Json::Value
authorize(
    jtx::Account const& account,
    ripple::uint256 const& issuanceID,
    std::optional<jtx::Account> const& holder)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfTransactionType.jsonName] = jss::CFTokenAuthorize;
    jv[sfCFTokenIssuanceID.jsonName] = to_string(issuanceID);
    if (holder)
        jv[sfCFTokenHolder.jsonName] = holder->human();

    return jv;
}

Json::Value
set(jtx::Account const& account,
    ripple::uint256 const& issuanceID,
    std::optional<jtx::Account> const& holder)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfTransactionType.jsonName] = jss::CFTokenIssuanceSet;
    jv[sfCFTokenIssuanceID.jsonName] = to_string(issuanceID);
    if (holder)
        jv[sfCFTokenHolder.jsonName] = holder->human();

    return jv;
}

}  // namespace cft

}  // namespace jtx
}  // namespace test
}  // namespace ripple
