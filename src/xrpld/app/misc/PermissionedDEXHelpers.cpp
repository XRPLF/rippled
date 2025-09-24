//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#include <xrpld/app/misc/PermissionedDEXHelpers.h>

#include <xrpl/ledger/CredentialHelpers.h>

namespace ripple {
namespace permissioned_dex {

bool
accountInDomain(
    ReadView const& view,
    AccountID const& account,
    Domain const& domainID)
{
    auto const sleDomain = view.read(keylet::permissionedDomain(domainID));
    if (!sleDomain)
        return false;

    // domain owner is in the domain
    if (sleDomain->getAccountID(sfOwner) == account)
        return true;

    auto const& credentials = sleDomain->getFieldArray(sfAcceptedCredentials);

    bool const inDomain = std::any_of(
        credentials.begin(), credentials.end(), [&](auto const& credential) {
            auto const sleCred = view.read(keylet::credential(
                account, credential[sfIssuer], credential[sfCredentialType]));
            if (!sleCred || !sleCred->isFlag(lsfAccepted))
                return false;

            return !credentials::checkExpired(
                sleCred, view.info().parentCloseTime);
        });

    return inDomain;
}

bool
offerInDomain(
    ReadView const& view,
    uint256 const& offerID,
    Domain const& domainID,
    beast::Journal j)
{
    auto const sleOffer = view.read(keylet::offer(offerID));

    // The following are defensive checks that should never happen, since this
    // function is used to check against the order book offers, which should not
    // have any of the following wrong behavior
    if (!sleOffer)
        return false;  // LCOV_EXCL_LINE
    if (!sleOffer->isFieldPresent(sfDomainID))
        return false;  // LCOV_EXCL_LINE
    if (sleOffer->getFieldH256(sfDomainID) != domainID)
        return false;  // LCOV_EXCL_LINE

    if (sleOffer->isFlag(lsfHybrid) &&
        !sleOffer->isFieldPresent(sfAdditionalBooks))
    {
        JLOG(j.error()) << "Hybrid offer " << offerID
                        << " missing AdditionalBooks field";
        return false;  // LCOV_EXCL_LINE
    }

    return accountInDomain(view, sleOffer->getAccountID(sfAccount), domainID);
}

}  // namespace permissioned_dex

}  // namespace ripple
