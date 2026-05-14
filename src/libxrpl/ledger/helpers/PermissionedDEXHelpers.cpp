/** @file
 *  Implements the Permissioned DEX membership predicates.
 *
 *  `accountInDomain` and `offerInDomain` are the two authorization gatekeepers
 *  for credential-gated order books. They are invoked both from transaction
 *  preclaim validation and from live order-book traversal in `OfferStream`.
 */
#include <xrpl/ledger/helpers/PermissionedDEXHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
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

/** Test whether an account currently qualifies as a member of a permissioned domain.
 *
 *  Applies a two-tier check: the domain's `sfOwner` is unconditionally a member
 *  (preventing bootstrap lock-out); all other accounts must hold at least one
 *  credential from `sfAcceptedCredentials` that carries `lsfAccepted` and has
 *  not expired. Expiry is evaluated against `parentCloseTime` — that value is
 *  finalized and deterministic across all validators, whereas the current close
 *  time is not yet committed at preclaim phase.
 *
 *  @param view      The read-only ledger view to query.
 *  @param account   The account whose domain membership is being tested.
 *  @param domainID  The identifier of the `PermissionedDomain` ledger object.
 *  @return `true` if @p account is the domain owner or holds at least one
 *      accepted, non-expired credential listed in the domain; `false` if the
 *      domain object does not exist or no qualifying credential is found.
 */
bool
accountInDomain(ReadView const& view, AccountID const& account, Domain const& domainID)
{
    auto const sleDomain = view.read(keylet::permissionedDomain(domainID));
    if (!sleDomain)
        return false;

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

/** Test whether a specific offer is still legitimately part of a permissioned domain.
 *
 *  Performs structural validation on the offer SLE (existence, `sfDomainID`
 *  presence and match) before delegating the live membership check to
 *  `accountInDomain`. The structural checks guard against internal invariant
 *  violations and are marked `LCOV_EXCL_LINE` because they cannot be triggered
 *  via any valid user-facing input path. Hybrid offers additionally require
 *  `sfAdditionalBooks`: post-`fixSecurity3_1_3` the array must contain exactly
 *  one entry; pre-amendment only its presence is required.
 *
 *  @param view      The read-only ledger view to query.
 *  @param offerID   The hash identifier of the offer SLE to validate.
 *  @param domainID  The permissioned domain the offer is expected to belong to.
 *  @param j         Journal used to log an error if a hybrid offer has a missing
 *      or malformed `sfAdditionalBooks` field.
 *  @return `true` if the offer passes all structural checks and its owner is
 *      currently a member of @p domainID; `false` otherwise.
 *
 *  @note When this returns `false`, `OfferStream` permanently removes the offer
 *      from the book via `permRmOffer`, handling the race between offer placement
 *      and subsequent credential expiry.
 */
bool
offerInDomain(
    ReadView const& view,
    uint256 const& offerID,
    Domain const& domainID,
    beast::Journal j)
{
    auto const sleOffer = view.read(keylet::offer(offerID));

    if (!sleOffer)
        return false;  // LCOV_EXCL_LINE
    if (!sleOffer->isFieldPresent(sfDomainID))
        return false;  // LCOV_EXCL_LINE
    if (sleOffer->getFieldH256(sfDomainID) != domainID)
        return false;  // LCOV_EXCL_LINE

    if (view.rules().enabled(fixSecurity3_1_3))
    {
        // post-fixSecurity3_1_3: a valid hybrid offer must have
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
        // pre-fixSecurity3_1_3: a valid hybrid offer must have
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
