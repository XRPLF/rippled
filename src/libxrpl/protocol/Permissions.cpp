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

#include <xrpl/protocol/Permissions.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

Permission::Permission()
{
    granularPermissionMap = {
        {"TrustlineAuthorize", TrustlineAuthorize},
        {"TrustlineFreeze", TrustlineFreeze},
        {"TrustlineUnfreeze", TrustlineUnfreeze},
        {"AccountDomainSet", AccountDomainSet},
        {"AccountEmailHashSet", AccountEmailHashSet},
        {"AccountMessageKeySet", AccountMessageKeySet},
        {"AccountTransferRateSet", AccountTransferRateSet},
        {"AccountTickSizeSet", AccountTickSizeSet},
        {"PaymentMint", PaymentMint},
        {"PaymentBurn", PaymentBurn},
        {"MPTokenIssuanceLock", MPTokenIssuanceLock},
        {"MPTokenIssuanceUnlock", MPTokenIssuanceUnlock}};

    granularTxTypeMap = {
        {TrustlineAuthorize, ttTRUST_SET},
        {TrustlineFreeze, ttTRUST_SET},
        {TrustlineUnfreeze, ttTRUST_SET},
        {AccountDomainSet, ttACCOUNT_SET},
        {AccountEmailHashSet, ttACCOUNT_SET},
        {AccountMessageKeySet, ttACCOUNT_SET},
        {AccountTransferRateSet, ttACCOUNT_SET},
        {AccountTickSizeSet, ttACCOUNT_SET},
        {PaymentMint, ttPAYMENT},
        {PaymentBurn, ttPAYMENT},
        {MPTokenIssuanceLock, ttMPTOKEN_ISSUANCE_SET},
        {MPTokenIssuanceUnlock, ttMPTOKEN_ISSUANCE_SET}};
}

Permission const&
Permission::getInstance()
{
    static Permission const instance;
    return instance;
}

std::optional<std::uint32_t>
Permission::getGranularValue(std::string const& name) const
{
    auto const it = granularPermissionMap.find(name);
    if (it != granularPermissionMap.end())
        return static_cast<uint32_t>(it->second);

    return std::nullopt;
}

std::optional<TxType>
Permission::getGranularTxType(GranularPermissionType const& gpType) const
{
    auto const it = granularTxTypeMap.find(gpType);
    if (it != granularTxTypeMap.end())
        return it->second;

    return std::nullopt;
}

bool
Permission::isProhibited(std::string const& name) const
{
    // We do not allow delegating the following transaction permissions to other
    // accounts for security reason.
    if (name == "AccountSet" || name == "SetRegularKey" ||
        name == "SignerListSet")
        return true;

    return false;
}

}  // namespace ripple
