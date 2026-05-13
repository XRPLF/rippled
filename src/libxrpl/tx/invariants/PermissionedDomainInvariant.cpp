/** @file
 *  Implements the `ValidPermissionedDomain` post-transaction invariant checker.
 *
 *  Every transaction that creates or modifies a `ltPERMISSIONED_DOMAIN` ledger
 *  entry must leave its `sfAcceptedCredentials` array in a structurally valid
 *  state: non-empty, within the size cap, with no duplicate entries, and in
 *  canonical sort order.  Violations are caught here — after `doApply` — as a
 *  hard safety net that rolls back the transaction if the transactor failed to
 *  enforce the rules itself.
 */

#include <xrpl/tx/invariants/PermissionedDomainInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>

#include <memory>
#include <vector>

namespace xrpl {

/** Record credential-array facts for a single modified `ltPERMISSIONED_DOMAIN`
 *  entry, to be evaluated in `finalize`.
 *
 *  Non-domain entries are silently ignored.  Only the post-transaction `after`
 *  state is examined; the pre-transaction state is irrelevant to whether the
 *  resulting ledger is valid.
 *
 *  Uniqueness is detected via `credentials::makeSorted`: that function returns
 *  an empty set when any duplicate `(sfIssuer, sfCredentialType)` pair is
 *  encountered, so `isUnique = !sorted.empty()`.  Sort order is then verified
 *  by walking the canonical set and the original array in lockstep — duplicates
 *  short-circuit this check because they render the sorted comparison
 *  meaningless.
 *
 *  @param isDel  True when the entry is being deleted by this transaction.
 *  @param before The ledger entry state before the transaction (may be null for
 *      newly created entries).
 *  @param after  The ledger entry state after the transaction (null for deleted
 *      entries; if null this call is a no-op).
 */
void
ValidPermissionedDomain::visitEntry(
    bool isDel,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    if (before && before->getType() != ltPERMISSIONED_DOMAIN)
        return;
    if (after && after->getType() != ltPERMISSIONED_DOMAIN)
        return;

    auto check = [isDel](std::vector<SleStatus>& sleStatus, std::shared_ptr<SLE const> const& sle) {
        auto const& credentials = sle->getFieldArray(sfAcceptedCredentials);
        auto const sorted = credentials::makeSorted(credentials);

        SleStatus ss{
            .credentialsSize = credentials.size(),
            .isSorted = false,
            // makeSorted returns empty on duplicates; non-empty means unique.
            .isUnique = !sorted.empty(),
            .isDelete = isDel};

        // If array have duplicates then all the other checks are invalid
        if (ss.isUnique)
        {
            unsigned i = 0;
            for (auto const& cred : sorted)
            {
                auto const& credTx = credentials[i++];
                ss.isSorted =
                    (cred.first == credTx[sfIssuer]) && (cred.second == credTx[sfCredentialType]);
                if (!ss.isSorted)
                    break;
            }
        }
        sleStatus.emplace_back(ss);
    };

    if (after)
        check(sleStatus_, after);
}

/** Apply the `ValidPermissionedDomain` invariant policy using the entry facts
 *  collected by `visitEntry`.
 *
 *  The strictness of the check is gated on the `fixPermissionedDomainInvariant`
 *  amendment:
 *
 *  - **Pre-amendment**: only validates after a successful
 *    `ttPERMISSIONED_DOMAIN_SET`; all other transaction types and all failures
 *    are passed unconditionally.
 *
 *  - **Post-amendment**: enforces a comprehensive policy —
 *    - A failed transaction must not have touched any domain entry at all.
 *    - At most one domain entry may be affected per transaction.
 *    - `ttPERMISSIONED_DOMAIN_SET`: must have modified (not deleted) exactly
 *      one entry, and its `sfAcceptedCredentials` array must be non-empty,
 *      within `kMAX_PERMISSIONED_DOMAIN_CREDENTIALS_ARRAY_SIZE` (10), unique,
 *      and sorted.
 *    - `ttPERMISSIONED_DOMAIN_DELETE`: must have deleted exactly one entry.
 *    - Any other transaction type: must not have affected any domain entry.
 *
 *  All failures are logged at `fatal` severity and return `false`, causing
 *  `ApplyContext` to roll back the transaction entirely.
 *
 *  @param tx     The transaction being finalized.
 *  @param result The TER code produced by `doApply`.
 *  @param view   The post-transaction read view, used to query active
 *      amendments.
 *  @param j      Journal for fatal-level diagnostic logging on failure.
 *  @return `true` if the invariant holds; `false` to veto the transaction.
 */
bool
ValidPermissionedDomain::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    auto check = [](SleStatus const& sleStatus, beast::Journal const& j) {
        if (!sleStatus.credentialsSize)
        {
            JLOG(j.fatal()) << "Invariant failed: permissioned domain with "
                               "no rules.";
            return false;
        }

        if (sleStatus.credentialsSize > kMAX_PERMISSIONED_DOMAIN_CREDENTIALS_ARRAY_SIZE)
        {
            JLOG(j.fatal()) << "Invariant failed: permissioned domain bad "
                               "credentials size "
                            << sleStatus.credentialsSize;
            return false;
        }

        if (!sleStatus.isUnique)
        {
            JLOG(j.fatal()) << "Invariant failed: permissioned domain credentials "
                               "aren't unique";
            return false;
        }

        if (!sleStatus.isSorted)
        {
            JLOG(j.fatal()) << "Invariant failed: permissioned domain credentials "
                               "aren't sorted";
            return false;
        }

        return true;
    };

    if (view.rules().enabled(fixPermissionedDomainInvariant))
    {
        if (!isTesSuccess(result))
        {
            // A failed transaction must leave all domain entries untouched.
            return sleStatus_.empty();
        }

        if (sleStatus_.size() > 1)
        {
            JLOG(j.fatal()) << "Invariant failed: transaction affected more "
                               "than 1 permissioned domain entry.";
            return false;
        }

        switch (tx.getTxnType())
        {
            case ttPERMISSIONED_DOMAIN_SET: {
                if (sleStatus_.empty())
                {
                    JLOG(j.fatal()) << "Invariant failed: no domain objects affected by "
                                       "PermissionedDomainSet";
                    return false;
                }

                auto const& sleStatus = sleStatus_[0];
                if (sleStatus.isDelete)
                {
                    JLOG(j.fatal()) << "Invariant failed: domain object "
                                       "deleted by PermissionedDomainSet";
                    return false;
                }
                return check(sleStatus, j);
            }
            case ttPERMISSIONED_DOMAIN_DELETE: {
                if (sleStatus_.empty())
                {
                    JLOG(j.fatal()) << "Invariant failed: no domain objects affected by "
                                       "PermissionedDomainDelete";
                    return false;
                }

                if (!sleStatus_[0].isDelete)
                {
                    JLOG(j.fatal()) << "Invariant failed: domain object "
                                       "modified, but not deleted by "
                                       "PermissionedDomainDelete";
                    return false;
                }
                return true;
            }
            default: {
                if (!sleStatus_.empty())
                {
                    JLOG(j.fatal()) << "Invariant failed: " << sleStatus_.size()
                                    << " domain object(s) affected by an "
                                       "unauthorized transaction. "
                                    << tx.getTxnType();
                    return false;
                }
                return true;
            }
        }
    }
    else
    {
        if (tx.getTxnType() != ttPERMISSIONED_DOMAIN_SET || !isTesSuccess(result) ||
            sleStatus_.empty())
            return true;
        return check(sleStatus_[0], j);
    }
}

}  // namespace xrpl
