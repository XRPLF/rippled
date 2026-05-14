/**
 * @file PermissionedDEXHelpers.h
 * @brief Domain membership predicates for the Permissioned DEX.
 *
 * Declares the two authorization gatekeepers used by `xrpl::permissioned_dex`
 * to enforce credential-based access control on restricted order books.
 * Both functions are called from transaction preclaim logic and from live
 * order-book traversal in `OfferStream`.
 */
#pragma once
#include <xrpl/ledger/View.h>

namespace xrpl::permissioned_dex {

/**
 * @brief Test whether an account currently qualifies as a member of a
 *        permissioned domain.
 *
 * Resolves the `PermissionedDomain` ledger object identified by @p domainID
 * and applies a two-tier membership test:
 *
 * 1. **Owner shortcut** — the domain's `sfOwner` is always considered a member,
 *    avoiding a bootstrap problem where the owner couldn't trade in their own
 *    domain.
 * 2. **Credential scan** — for all other accounts, the function iterates
 *    `sfAcceptedCredentials` and returns `true` as soon as it finds a
 *    credential issued to @p account that (a) carries the `lsfAccepted` flag
 *    and (b) has not expired according to `credentials::checkExpired` evaluated
 *    against the ledger's `parentCloseTime`.
 *
 * Expiry is evaluated against `parentCloseTime` (not wall time) so that all
 * validators reach the same deterministic result regardless of local clock skew.
 *
 * @param view      The read-only ledger view to query.
 * @param account   The account whose domain membership is being tested.
 * @param domainID  The identifier of the `PermissionedDomain` ledger object.
 * @return `true` if @p account is the domain owner or holds at least one
 *         accepted, non-expired credential listed in the domain; `false` if the
 *         domain object does not exist, or if no qualifying credential is found.
 *
 * @note Called from `OfferCreate` preclaim (rejects with `tecNO_PERMISSION` if
 *     `false`) and twice from `Payment` preclaim — once for the sender, once for
 *     the destination — since a domain payment requires both parties to be
 *     members. Also called internally by `offerInDomain`.
 */
[[nodiscard]] bool
accountInDomain(ReadView const& view, AccountID const& account, Domain const& domainID);

/**
 * @brief Test whether a specific offer is still legitimately part of a
 *        permissioned domain at the time it is being consumed.
 *
 * Called by `OfferStream` during order-book traversal to handle the race
 * between offer creation and subsequent credential expiry. An offer that was
 * valid when placed may become invalid if the owner's credentials expire before
 * the offer is matched. When this function returns `false`, `OfferStream`
 * removes the offer from the book immediately (`permRmOffer`) instead of
 * matching it.
 *
 * The function performs the following checks in order:
 * - Offer SLE must exist (defensive; should not occur in a well-formed book).
 * - Offer must carry `sfDomainID` (defensive; should not occur).
 * - `sfDomainID` must match @p domainID (defensive; should not occur).
 * - **Post-`fixCleanup3_1_3`**: a hybrid offer (`lsfHybrid`) must have
 *   `sfAdditionalBooks` present with exactly one entry; a violation is logged
 *   as an error and `false` is returned.
 * - **Pre-`fixCleanup3_1_3`**: a hybrid offer must have `sfAdditionalBooks`
 *   present (size is not validated).
 * - Delegates the final membership check to `accountInDomain` for the offer's
 *   owner (`sfAccount`).
 *
 * The three defensive checks are marked `LCOV_EXCL_LINE`; they guard against
 * invariant violations that cannot occur under normal operation but are retained
 * as safety nets.
 *
 * @param view      The read-only ledger view to query.
 * @param offerID   The hash identifier of the offer SLE to validate.
 * @param domainID  The permissioned domain the offer is expected to belong to.
 * @param j         Journal used to log an error if a hybrid offer has a missing
 *     or malformed `sfAdditionalBooks` field.
 * @return `true` if the offer passes all structural checks and its owner is
 *         currently a member of @p domainID; `false` otherwise.
 *
 * @note The `fixCleanup3_1_3` amendment tightens hybrid-offer validation from
 *     a presence-only check on `sfAdditionalBooks` to a presence-plus-size-one
 *     check. Both code paths must be preserved for deterministic historic replay.
 */
[[nodiscard]] bool
offerInDomain(
    ReadView const& view,
    uint256 const& offerID,
    Domain const& domainID,
    beast::Journal j);

}  // namespace xrpl::permissioned_dex
