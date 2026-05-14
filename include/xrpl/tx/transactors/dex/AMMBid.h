#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for the AMM continuous auction slot bid (XLS-30d).
 *
 *  Any LP holding tokens for an AMM pool may bid LP tokens for a 24-hour
 *  trading-fee discount slot, subdivided into 20 equal intervals (~72 min
 *  each). Auction slot states:
 *  - **Empty** — no current holder; minimum price applies.
 *  - **Occupied** — holder is in intervals 0–18; outbid formula applies.
 *  - **Tailing** — holder is in the final interval; holder pays minimum price
 *    only and receives no refund, discouraging last-minute camping.
 *
 *  Pricing curve (X = price paid by current holder, x = fraction of time used):
 *  @code
 *  computedPrice = X * 1.05 * (1 - x^60) + minSlotPrice
 *  @endcode
 *  The `x^60` term decays rapidly early in the slot and flattens near expiry.
 *  The 5 % multiplier prevents token-free squatting.
 *
 *  Bid revenue is split: `(1 - x) * X` LP tokens are refunded to the
 *  outgoing holder; the remainder is burned, increasing the proportional
 *  share of all remaining LPs.
 *
 *  All ledger mutations are applied in a `Sandbox` and committed atomically
 *  only on success.
 *
 *  @see https://github.com/XRPLF/XRPL-Standards/discussions/78
 */
class AMMBid : public Transactor
{
public:
    /** Normal consequences: bid consumes a bounded LP token amount and does not
     *  block other transactions from the same account while queued. */
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit AMMBid(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Gate the transaction on the AMM and MPT amendments.
     *
     *  Returns false (yielding `temDISABLED`) if the base AMM feature is not
     *  active, or if either pool asset is an MPT and `featureMPTokensV2` is not
     *  yet enabled.
     *
     *  @param ctx  Preflight context providing the active ledger rules.
     *  @return true if the transaction type is enabled; false otherwise.
     */
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    /** Stateless validation of the bid transaction fields.
     *
     *  Rejects malformed asset pairs, validates that `sfBidMin` and `sfBidMax`
     *  are well-formed LP token amounts when present, and enforces that
     *  `sfAuthAccounts` contains at most `kAUCTION_SLOT_MAX_AUTH_ACCOUNTS`
     *  entries. Under `fixAMMv1_3`, additionally rejects duplicate accounts and
     *  self-authorization in `sfAuthAccounts`.
     *
     *  @param ctx  Preflight context (no ledger access).
     *  @return `tesSUCCESS` or a `tem*` code.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Read-only ledger validation for the bid transaction.
     *
     *  Verifies the AMM object exists for the specified asset pair, the pool is
     *  not empty (`sfLPTokenBalance != 0`), all `sfAuthAccounts` entries
     *  reference existing on-ledger accounts, the submitter holds LP tokens, and
     *  any `sfBidMin`/`sfBidMax` amounts use the correct LP token asset and do
     *  not exceed the submitter's balance or the total pool balance.
     *
     *  @param ctx  Preclaim context with read-only ledger view.
     *  @return `tesSUCCESS`, a `ter*` retryable code, or a fee-claiming `tec*`
     *      code.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Execute the auction slot bid against a `Sandbox` view.
     *
     *  Delegates to the file-local `applyBid` helper, which computes the bid
     *  price, refunds the outgoing slot holder (if any), burns the remainder of
     *  the payment as LP tokens, and writes the updated `sfAuctionSlot` object
     *  inside `ltAMM`. The sandbox is committed to the real ledger view only on
     *  full success; any failure leaves the ledger untouched.
     *
     *  @return `tesSUCCESS` on success, or `tecAMM_FAILED` /
     *      `tecAMM_INVALID_TOKENS` / `tecINTERNAL` on failure.
     */
    TER
    doApply() override;

    /** No-op stub; no transaction-specific invariant entries for `AMMBid` yet.
     *
     *  @param isDelete  True if the entry was erased.
     *  @param before    Entry state before the transaction.
     *  @param after     Entry state after the transaction.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** No-op stub; no transaction-specific post-conditions for `AMMBid` yet.
     *
     *  Always returns true. Reserved for future transaction-specific invariants.
     *
     *  @param tx     The transaction being applied.
     *  @param result The tentative TER result.
     *  @param fee    Fee consumed by the transaction.
     *  @param view   Read-only ledger view after the transaction.
     *  @param j      Journal for logging.
     *  @return true unconditionally.
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
