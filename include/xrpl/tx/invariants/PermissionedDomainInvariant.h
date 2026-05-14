/** @file
 *  Declares `ValidPermissionedDomain`, the invariant checker that enforces the
 *  structural integrity of `ltPERMISSIONED_DOMAIN` ledger entries.
 */

#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <vector>

namespace xrpl {

/** Invariant checker that enforces the structural integrity of
 *  `ltPERMISSIONED_DOMAIN` ledger entries.
 *
 *  Every `ltPERMISSIONED_DOMAIN` entry must leave its `sfAcceptedCredentials`
 *  array in a valid state: non-empty, within the
 *  `kMAX_PERMISSIONED_DOMAIN_CREDENTIALS_ARRAY_SIZE` (10) cap, free of
 *  duplicate `(sfIssuer, sfCredentialType)` pairs, and in canonical
 *  lexicographic sort order as defined by `credentials::makeSorted`.
 *
 *  These guarantees are foundational: downstream consumers such as
 *  `credentials::validDomain` iterate `sfAcceptedCredentials` without
 *  re-validating its structure on every access.
 *
 *  Follows the two-phase `visitEntry` / `finalize` protocol required of all
 *  invariant checkers registered in `InvariantChecks`. `visitEntry` snapshots
 *  credential-array facts into the `sleStatus_` vector; `finalize` interprets
 *  those facts in the context of the completed transaction.
 *
 *  The strictness of `finalize` is gated on the `fixPermissionedDomainInvariant`
 *  amendment. Without the amendment, only a successful `ttPERMISSIONED_DOMAIN_SET`
 *  is checked. With the amendment, the invariant also asserts that failed
 *  transactions leave all domain entries untouched, that no transaction affects
 *  more than one domain entry, that `ttPERMISSIONED_DOMAIN_DELETE` deletes
 *  exactly one entry, and that no unauthorized transaction type touches a
 *  domain entry at all.
 *
 *  @see ValidPermissionedDEX
 *  @see InvariantChecker_PROTOTYPE for the duck-typed interface.
 */
class ValidPermissionedDomain
{
    /** Snapshot of credential-array facts for a single `ltPERMISSIONED_DOMAIN`
     *  entry, recorded by `visitEntry` and consumed by `finalize`.
     *
     *  All fields are derived from the post-transaction (`after`) SLE state.
     *  `isSorted` and `isUnique` are pre-computed in `visitEntry` so
     *  `finalize` can evaluate them without re-reading the SLE through
     *  `ReadView`. The sort check short-circuits on the first out-of-order
     *  pair (O(n)); `isUnique` relies on `credentials::makeSorted` returning
     *  an empty set when any duplicate pair is present.
     */
    struct SleStatus
    {
        /** Raw count of entries in `sfAcceptedCredentials`.
         *  Zero means no rules exist (always invalid); greater than
         *  `kMAX_PERMISSIONED_DOMAIN_CREDENTIALS_ARRAY_SIZE` means the array
         *  is oversized.
         */
        std::size_t credentialsSize{0};

        /** True when the array's iteration order matches the canonical order
         *  produced by `credentials::makeSorted`.  Only meaningful when
         *  `isUnique` is also true; duplicates make the sorted comparison
         *  meaningless.
         */
        bool isSorted = false;

        /** True when `credentials::makeSorted` returned a non-empty result,
         *  indicating no duplicate `(sfIssuer, sfCredentialType)` pairs exist.
         */
        bool isUnique = false;

        /** Propagated from the `isDel` argument of `visitEntry`; allows
         *  `finalize` to distinguish a creation/modification from a deletion
         *  without re-querying the view.
         */
        bool isDelete = false;
    };

    /** Accumulated observations, one per `ltPERMISSIONED_DOMAIN` entry
     *  touched by the current transaction.
     */
    std::vector<SleStatus> sleStatus_;

public:
    /** Record credential-array facts for a single modified `ltPERMISSIONED_DOMAIN`
     *  entry.
     *
     *  Non-domain entries are ignored immediately.  Only the post-transaction
     *  `after` state is examined; the pre-transaction state is irrelevant to
     *  whether the resulting ledger is structurally valid.  Deleted entries
     *  (where `after` is null) are recorded as deletions with zero credential
     *  count — `finalize` uses `isDelete` to distinguish them.
     *
     *  @param isDel  True when the entry is being deleted by this transaction.
     *  @param before The ledger entry state before the transaction; may be null
     *      for newly created entries.  Unused by this checker.
     *  @param after  The ledger entry state after the transaction; null for
     *      deleted entries, in which case this call is a no-op.
     */
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    /** Apply the invariant policy using the entry facts collected by `visitEntry`.
     *
     *  Behavior depends on whether `fixPermissionedDomainInvariant` is active:
     *
     *  **Without the amendment (legacy path):** only validates after a
     *  successful `ttPERMISSIONED_DOMAIN_SET` that touched at least one domain
     *  entry.  All other transaction types and all failed transactions pass
     *  unconditionally.
     *
     *  **With the amendment (strict path):**
     *  - A failed transaction must not have mutated any domain entry
     *    (`sleStatus_` must be empty).
     *  - At most one domain entry may be affected per transaction.
     *  - `ttPERMISSIONED_DOMAIN_SET`: must have modified (not deleted) exactly
     *    one entry whose `sfAcceptedCredentials` array satisfies all four
     *    constraints (non-empty, within cap, unique, sorted).
     *  - `ttPERMISSIONED_DOMAIN_DELETE`: must have deleted exactly one entry
     *    and must not have modified it.
     *  - Any other transaction type: must not have affected any domain entry.
     *
     *  All failures are logged at `fatal` severity before returning `false`,
     *  which causes `ApplyContext` to roll back the transaction.
     *
     *  @param tx     The transaction being finalized.
     *  @param result The `TER` code produced by `doApply`.
     *  @param fee    The fee charged (unused by this checker).
     *  @param view   Post-transaction read view; used to query whether
     *      `fixPermissionedDomainInvariant` is active.
     *  @param j      Journal for `fatal`-level diagnostic logging on failure.
     *  @return `true` if the invariant holds; `false` to veto the transaction.
     */
    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
