#include <xrpl/ledger/helpers/PermissionedDEXHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/UintTypes.h>

#include <algorithm>

namespace xrpl::permissioned_dex {

bool
accountInDomain(ReadView const& view, AccountID const& account, Domain const& domainID)
{
    // Avoid constructing a zero-key PermissionedDomain keylet.
    // keylet::permissionedDomain(uint256) uses the DomainID as the ledger key.
    if (view.rules().enabled(fixCleanup3_2_0) && domainID == beast::kZero)
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::permissioned_dex::accountInDomain : domainID is zero");
        return false;
        // LCOV_EXCL_STOP
    }

    auto const sleDomain = view.read(keylet::permissionedDomain(domainID));
    if (!sleDomain)
        return false;

    // domain owner is in the domain
    if (sleDomain->getAccountID(sfOwner) == account)
        return true;

    auto const& credentials = sleDomain->getFieldArray(sfAcceptedCredentials);

    bool const inDomain = std::ranges::any_of(credentials, [&](auto const& credential) {
        auto const sleCred = view.read(
            keylet::credential(account, credential[sfIssuer], credential[sfCredentialType]));
        if (!sleCred || !sleCred->isFlag(lsfAccepted))
            return false;

        return !credentials::checkExpired(*sleCred, view.header().parentCloseTime);
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

    if (view.rules().enabled(fixCleanup3_1_3))
    {
        // post-fixCleanup3_1_3: a valid hybrid offer must have
        // sfAdditionalBooks present with exactly 1 entry
        if (sleOffer->isFlag(lsfHybrid) &&
            (!sleOffer->isFieldPresent(sfAdditionalBooks) ||
             sleOffer->getFieldArray(sfAdditionalBooks).size() != 1))
        {
            JLOG(j.error()) << "Hybrid offer " << offerID
                            << " missing or malformed AdditionalBooks field";
            return false;  // LCOV_EXCL_LINE
        }
    }
    else
    {
        // pre-fixCleanup3_1_3: a valid hybrid offer must have
        // sfAdditionalBooks present (size is not checked)
        if (sleOffer->isFlag(lsfHybrid) && !sleOffer->isFieldPresent(sfAdditionalBooks))
        {
            JLOG(j.error()) << "Hybrid offer " << offerID << " missing AdditionalBooks field";
            return false;  // LCOV_EXCL_LINE
        }
    }

    return accountInDomain(view, sleOffer->getAccountID(sfAccount), domainID);
}

}  // namespace xrpl::permissioned_dex
