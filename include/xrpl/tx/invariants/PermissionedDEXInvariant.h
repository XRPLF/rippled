/** @file
 *  Declares `ValidPermissionedDEX`, the invariant checker that enforces
 *  domain isolation for the Permissioned DEX feature.
 */

#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

namespace xrpl {

/** Invariant checker that enforces the isolation contract of the Permissioned DEX.
 *
 *  Verifies that every successful `ttPAYMENT` or `ttOFFER_CREATE` transaction
 *  that carries an `sfDomainID` operates exclusively within its declared
 *  domain: it must not touch order-book directories or offers belonging to a
 *  different domain, and it must not interact with regular (non-domain) offers.
 *  Additionally ensures that any hybrid offer produced by an `OfferCreate` is
 *  structurally well-formed.
 *
 *  The definition of "well-formed" for hybrid offers is amendment-gated:
 *  before `fixCleanup3_1_3` activates, the checker rejects hybrids with a
 *  missing `sfDomainID`, missing `sfAdditionalBooks`, or an
 *  `sfAdditionalBooks` array larger than 1; after the amendment activates,
 *  the size must be exactly 1 (an empty array is also rejected).
 *
 *  Follows the two-phase `visitEntry` / `finalize` protocol required of all
 *  invariant checkers registered in `InvariantChecks`.
 *
 *  @see ValidPermissionedDomain
 */
class ValidPermissionedDEX
{
    bool regularOffers_ = false;
    bool badHybridsOld_ = false;  // pre-fixCleanup3_1_3: missing field/domain or size > 1
    bool badHybrids_ = false;     // post-fixCleanup3_1_3: also catches size == 0 (size != 1)
    hash_set<uint256> domains_;

public:
    /** Accumulate domain and offer state from a single modified ledger entry.
     *
     *  Called once per modified entry before `finalize`. Inspects only the
     *  post-transaction (`after`) snapshot; `before` is unused. For each
     *  `ltDIR_NODE` or `ltOFFER` entry, records any `sfDomainID` encountered
     *  into `domains_`. Sets `regularOffers_` if an offer lacks `sfDomainID`.
     *  Sets `badHybrids_` / `badHybridsOld_` if a hybrid offer (`lsfHybrid`)
     *  is structurally malformed (missing domain, missing `sfAdditionalBooks`,
     *  or wrong array size).
     *
     *  @param isDelete True if the entry is being deleted (ignored).
     *  @param before   Pre-transaction SLE snapshot (unused by this checker).
     *  @param after    Post-transaction SLE snapshot; may be null for deletions.
     */
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    /** Evaluate all accumulated state and return whether the invariant holds.
     *
     *  Only active for successful (`isTesSuccess`) `ttPAYMENT` and
     *  `ttOFFER_CREATE` transactions; all others pass unconditionally.
     *  Transactions that carry no `sfDomainID` also pass unconditionally —
     *  unpermissioned transactions are not constrained by this checker.
     *
     *  Checks (in order):
     *  - For `ttOFFER_CREATE`: no hybrid offer is structurally malformed.
     *    Which malformation predicate applies depends on whether
     *    `fixCleanup3_1_3` is active in the current rule set (pre-amendment:
     *    `badHybridsOld_`; post-amendment: `badHybrids_`).
     *  - The `ltPERMISSIONED_DOMAIN` referenced by `sfDomainID` exists in
     *    the ledger — guards against a domain deleted within the same batch.
     *  - Every domain ID recorded in `domains_` matches the transaction's own
     *    domain; any foreign domain indicates cross-domain contamination.
     *  - No regular (non-domain) offers were touched.
     *
     *  @param tx     The transaction being applied.
     *  @param result The TER code returned by `doApply`.
     *  @param fee    The fee charged (unused by this checker).
     *  @param view   Read-only ledger view used to verify domain existence.
     *  @param j      Journal for fatal-level diagnostic logging on failure.
     *  @return True if the invariant holds; false if a violation was detected.
     */
    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
