#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Governance transactor that lets liquidity providers vote on an AMM's trading fee.
 *
 *  Each LP submits a preferred `sfTradingFee` weighted by their share of the
 *  pool's total `LPToken` supply.  The AMM object stores up to
 *  `kVOTE_MAX_SLOTS` (8) `VoteEntry` records; the resulting fee is recomputed
 *  as a capital-weighted average `sum(fee_i * tokens_i) / sum(tokens_i)` over
 *  all active slots after every vote.
 *
 *  **Slot management:** stale entries (LPs that no longer hold tokens) are
 *  pruned on every vote.  When all eight slots are occupied and the voter is
 *  not already present, the entry with the fewest tokens is a candidate for
 *  eviction — but only if the incoming LP holds strictly more tokens, or equal
 *  tokens and proposes a higher fee.  Ties are broken deterministically by
 *  account ID.  If no slot can be displaced, the transaction succeeds but does
 *  not alter the slot array.
 *
 *  **Auction slot coupling:** after updating `sfTradingFee`, the transactor
 *  also propagates a new `sfDiscountedFee` (`tradingFee /
 *  kAUCTION_SLOT_DISCOUNTED_FEE_FRACTION`) into `sfAuctionSlot` if one is
 *  present, keeping the auction winner's price advantage in sync with
 *  governance decisions.  When either fee rounds to zero the field is removed
 *  rather than stored as zero.
 *
 *  @see [XLS-30d: AMM fee-vote governance](https://github.com/XRPLF/XRPL-Standards/discussions/78)
 */
class AMMVote : public Transactor
{
public:
    /** Uses standard fee and sequence-number consequences; no custom scaling. */
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit AMMVote(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Amendment gate for AMM vote transactions.
     *
     *  Returns `false` (causing `invokePreflight` to emit `temDISABLED`) when
     *  the core AMM amendment (`featureAMM` + `fixUniversalNumber`) is not
     *  enabled.  Additionally requires `featureMPTokensV2` when either pool
     *  asset is an `MPTIssue`.
     *
     *  @param ctx  Preflight context providing the active rule set.
     *  @return `true` if the transaction may proceed; `false` to disable it.
     */
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    /** Stateless validation of the vote transaction fields.
     *
     *  Verifies that the asset pair is structurally coherent via
     *  `invalidAMMAssetPair` and that `sfTradingFee` does not exceed
     *  `kTRADING_FEE_THRESHOLD` (1000 = 1%).  No ledger access is performed.
     *
     *  @param ctx  Preflight context carrying the transaction and rule set.
     *  @return `tesSUCCESS`, `temBAD_FEE` if the fee is out of range, or a
     *      `tem*` code from `invalidAMMAssetPair` for a malformed asset pair.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Read-only ledger checks before the vote is applied.
     *
     *  Confirms that the AMM object exists for the specified asset pair
     *  (`terNO_AMM`), that the pool is not empty — a zero `sfLPTokenBalance`
     *  means there are no active LPs (`tecAMM_EMPTY`) — and that the
     *  submitting account holds a non-zero LPToken balance (`tecAMM_INVALID_TOKENS`).
     *  An account with no stake in the pool has no standing to influence its fee.
     *
     *  @param ctx  Preclaim context providing the read-only ledger view.
     *  @return `tesSUCCESS` if all checks pass; `terNO_AMM`,
     *      `tecAMM_EMPTY`, or `tecAMM_INVALID_TOKENS` otherwise.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Apply the vote and update the AMM object's fee governance state.
     *
     *  Wraps execution in a `Sandbox` view so all ledger mutations are
     *  committed atomically only on success.  The vote logic (slot pruning,
     *  eviction, weighted-average fee recalculation, and auction slot
     *  discount propagation) is delegated to the file-scoped `applyVote`
     *  helper in the implementation.
     *
     *  @return `tesSUCCESS` on success; `tecINTERNAL` if the AMM SLE
     *      cannot be peeked (indicates ledger corruption, unreachable under
     *      normal conditions).
     */
    TER
    doApply() override;

    /** No transaction-specific invariant entries to visit (future work).
     *
     *  @param isDelete  Whether the SLE is being deleted.
     *  @param before    SLE state before the transaction.
     *  @param after     SLE state after the transaction.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** No transaction-specific invariants to finalize (future work).
     *
     *  @param tx      The applied transaction.
     *  @param result  The TER code returned by `doApply`.
     *  @param fee     The XRP fee charged.
     *  @param view    Read-only view of the post-apply ledger state.
     *  @param j       Journal for diagnostic logging.
     *  @return Always `true`.
     */
    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;
};

}  // namespace xrpl
