#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>

namespace xrpl {

/** Invariant: every touched `ltNFTOKEN_PAGE` entry must satisfy the
 *  structural rules of the NFToken directory after each transaction.
 *
 *  NFToken pages use a composite 256-bit key: the high 160 bits are the
 *  owning account and the low 96 bits are the page's *high limit* — the
 *  exclusive upper bound on which token IDs (by their own low 96 bits)
 *  belong on that page. Six structural properties are checked on every
 *  snapshot (before and after) of each visited `ltNFTOKEN_PAGE`:
 *
 *  1. **Link ownership and ordering**: `sfPreviousPageMin`/`sfNextPageMin`
 *     high 160 bits must match the current page's account; low 96 bits
 *     must be strictly ordered (`prev < current < next`).
 *  2. **Size**: 1–`dirMaxTokensPerPage` (32) tokens, unless the page is
 *     being deleted (empty is permitted only then).
 *  3. **Membership**: each token's page-bits must fall in `[loLimit, hiLimit)`.
 *  4. **Sort**: tokens within the page must be strictly ascending under
 *     `nft::compareTokens()`.
 *  5. **URI**: an `sfURI` field, if present, must be non-empty.
 *
 *  Two additional cross-snapshot checks are gated on `fixNFTokenPageLinks`
 *  to avoid penalising pre-amendment history:
 *  - Deleting the final page (all 96 page-bits set) while
 *    `sfPreviousPageMin` is still present would orphan the directory.
 *  - A non-final page silently losing `sfNextPageMin` breaks forward
 *    traversal of the directory.
 *
 *  Failures are recorded in boolean flags; `finalize` reports them.
 *
 *  @see InvariantChecker_PROTOTYPE for the two-phase interface contract.
 *  @see NFTokenCountTracking for the paired mint/burn counter checker.
 */
class ValidNFTokenPage
{
    /** True if any token's page-bits fall outside `[loLimit, hiLimit)`. */
    bool badEntry_ = false;
    /** True if any page-link crosses accounts or violates strict ordering. */
    bool badLink_ = false;
    /** True if tokens within any page are not strictly ascending. */
    bool badSort_ = false;
    /** True if any token carries an empty `sfURI` field. */
    bool badURI_ = false;
    /** True if any non-deleted page has zero tokens or more than 32 tokens. */
    bool invalidSize_ = false;
    /** True if the final page (all 96 page-bits == 1) was deleted while
     *  `sfPreviousPageMin` was still present.  Gated on `fixNFTokenPageLinks`. */
    bool deletedFinalPage_ = false;
    /** True if a non-final page lost `sfNextPageMin` without being deleted.
     *  Gated on `fixNFTokenPageLinks`. */
    bool deletedLink_ = false;

public:
    /** Inspect one touched `ltNFTOKEN_PAGE` entry for structural integrity.
     *
     *  Non-NFT-page SLEs are silently ignored.  For each snapshot that is
     *  present the inner check validates links, size, membership, sort, and
     *  URI.  The two amendment-gated transition checks (`deletedFinalPage_`,
     *  `deletedLink_`) are applied across the `before`→`after` pair.
     *
     *  @param isDelete True if the SLE is being removed from the ledger;
     *      allows an empty token array on the `before` snapshot.
     *  @param before Snapshot of the entry before the transaction, or nullptr
     *      if the entry is being created.
     *  @param after Snapshot of the entry after the transaction, or nullptr if
     *      the entry is being deleted.
     */
    void
    visitEntry(bool isDelete, std::shared_ptr<SLE const> const& before, std::shared_ptr<SLE const> const& after);

    /** Render a pass/fail verdict for all accumulated NFT page checks.
     *
     *  Reports the first set flag and returns `false` (triggering
     *  `tecINVARIANT_FAILED`).  The `deletedFinalPage_` and `deletedLink_`
     *  checks are enforced only when `fixNFTokenPageLinks` is active, so
     *  pre-amendment historical replay is unaffected.
     *
     *  @param tx Unused.
     *  @param result Unused.
     *  @param fee Unused.
     *  @param view The post-transaction ledger view; used to query whether
     *      `fixNFTokenPageLinks` is active.
     *  @param j Journal for fatal-level diagnostics on failure.
     *  @return True if all NFT page invariants pass; false on any violation.
     */
    [[nodiscard]] bool
    finalize(STTx const& tx, TER const result, XRPAmount const fee, ReadView const& view, beast::Journal const& j) const;
};

/** Invariant: the global `sfMintedNFTokens` and `sfBurnedNFTokens` totals
 *  across all touched account roots must change only as the transaction type
 *  and result code require.
 *
 *  `visitEntry` accumulates pre- and post-transaction values of both fields
 *  across every `ltACCOUNT_ROOT` touched by the transaction.  `finalize`
 *  then branches on the `ChangeNftCounts` privilege:
 *
 *  - **Without privilege** (all transactions except `ttNFTOKEN_MINT`/`BURN`):
 *    both totals must be completely unchanged.
 *  - **`ttNFTOKEN_MINT`** success: minted total must strictly increase; burned
 *    total must be unchanged.  On failure: both totals must be unchanged.
 *  - **`ttNFTOKEN_BURN`** success: burned total must strictly increase; minted
 *    total must be unchanged.  On failure: both totals must be unchanged.
 *
 *  Tracking global sums rather than per-account deltas is intentional: a
 *  legitimate mint or burn touches exactly one account's counters, so
 *  global-sum equality is both necessary and sufficient. Strict inequality
 *  (`>=` rather than `==`) on success additionally catches counter
 *  wrap-around and incorrect field rewrites.
 *
 *  @note Failed transactions carry no privileges, so the no-privilege path
 *      catches counter mutations during a failed mint/burn — a critical
 *      guard against exploit code that corrupts counters on failure.
 *  @see InvariantChecker_PROTOTYPE for the two-phase interface contract.
 *  @see ValidNFTokenPage for the paired page-structure checker.
 */
class NFTokenCountTracking
{
    /** Sum of `sfMintedNFTokens` across all touched account roots, before the transaction. */
    std::uint32_t beforeMintedTotal_ = 0;
    /** Sum of `sfBurnedNFTokens` across all touched account roots, before the transaction. */
    std::uint32_t beforeBurnedTotal_ = 0;
    /** Sum of `sfMintedNFTokens` across all touched account roots, after the transaction. */
    std::uint32_t afterMintedTotal_ = 0;
    /** Sum of `sfBurnedNFTokens` across all touched account roots, after the transaction. */
    std::uint32_t afterBurnedTotal_ = 0;

public:
    /** Accumulate `sfMintedNFTokens` and `sfBurnedNFTokens` totals.
     *
     *  Non-account-root SLEs are silently skipped.  Absent fields are treated
     *  as zero via `.value_or(0)`.
     *
     *  @param isDelete Unused.
     *  @param before Pre-transaction snapshot, or nullptr for new entries.
     *  @param after Post-transaction snapshot, or nullptr for deleted entries.
     */
    void
    visitEntry(bool isDelete, std::shared_ptr<SLE const> const& before, std::shared_ptr<SLE const> const& after);

    /** Validate NFT mint/burn counter invariants for the completed transaction.
     *
     *  @param tx The transaction; used for type and privilege checks.
     *  @param result The transaction result code; distinguishes success from
     *      failure for the mint/burn symmetry rules.
     *  @param fee Unused.
     *  @param view Unused.
     *  @param j Journal for fatal-level diagnostics on failure.
     *  @return True if all count invariants pass; false on any violation.
     */
    [[nodiscard]] bool
    finalize(STTx const& tx, TER const result, XRPAmount const fee, ReadView const& view, beast::Journal const& j) const;
};

}  // namespace xrpl
