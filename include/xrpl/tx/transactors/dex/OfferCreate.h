/** @file
 *  Declares the OfferCreate transactor for the XRPL decentralized exchange.
 *
 *  OfferCreate processes `ttOFFER_CREATE` transactions: it validates offer
 *  fields, optionally cancels a pre-existing offer, attempts immediate crossing
 *  against resting orders via the payment engine, and — if any unfilled amount
 *  remains — writes a new offer ledger entry into the appropriate order-book
 *  directory.
 */

#pragma once

#include <xrpl/protocol/Quality.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

class PaymentSandbox;
class Sandbox;

/** Transactor for `ttOFFER_CREATE` transactions on the XRPL DEX.
 *
 *  Handles the full lifecycle of offer creation: structural validation in
 *  `preflight`, ledger-state checks in `preclaim`, and the three-stage
 *  `doApply` sequence of (optional) offer cancellation, crossing via the
 *  payment engine (`flowCross`), and conditional placement of any residual
 *  offer on the order book.
 *
 *  Uses `ConsequencesFactoryType::Custom` so the transaction queue can
 *  accurately bound the maximum XRP a queued offer could spend (see
 *  `makeTxConsequences`).
 *
 *  @see OfferCancel for the simpler remove-only counterpart.
 */
class OfferCreate : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Custom;

    /** Construct the transactor for the given apply context. */
    explicit OfferCreate(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Compute the maximum XRP this offer could spend, for queue accounting.
     *
     *  If `sfTakerGets` is native (XRP), returns that amount as the upper
     *  bound on XRP consumed. For IOU-only offers, the XRP spend is zero.
     *  This prevents over-reserving queue capacity for non-XRP offers.
     *
     *  @param ctx Preflight context supplying the transaction fields.
     *  @return `TxConsequences` carrying the calculated maximum XRP spend.
     */
    static TxConsequences
    makeTxConsequences(PreflightContext const& ctx);

    /** Gate the transaction on the protocol amendments it requires.
     *
     *  Rejects the transaction (`temDISABLED`) if `sfDomainID` is present but
     *  `featurePermissionedDEX` is not enabled, or if either `sfTakerPays` or
     *  `sfTakerGets` carries an `MPTIssue` without `featureMPTokensV2`.
     *
     *  @param ctx Preflight context supplying transaction fields and rules.
     *  @return `true` if all required amendments are active; `false` otherwise.
     */
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    /** Return the bitmask of flags accepted by this transaction type.
     *
     *  The base mask is `tfOfferCreateMask`. When `featurePermissionedDEX` is
     *  *not* active, `tfHybrid` is OR-ed in so that `preflight0` rejects any
     *  transaction that sets it.
     *
     *  @param ctx Preflight context supplying the active amendment rules.
     *  @return 32-bit mask of permitted flag bits.
     */
    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    /** Validate the structural integrity of the transaction fields.
     *
     *  Checks (in order): `tfHybrid` requires `sfDomainID`; mutual exclusivity
     *  of `tfImmediateOrCancel` and `tfFillOrKill`; non-zero expiration if
     *  present; non-zero cancel sequence if present; legal amounts for both
     *  sides; no XRP-for-XRP or same-asset offers; no bad currency codes; and
     *  native/issuer field consistency. All rejections are `tem*` codes — no
     *  fee is charged.
     *
     *  @param ctx Preflight context supplying transaction fields and rules.
     *  @return `tesSUCCESS` on success, or a `tem*` code on any field error.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Check ledger-state preconditions against a read-only view.
     *
     *  Verifies: the submitting account exists; neither asset is globally
     *  frozen; the account holds sufficient funds to back the offer
     *  (`tecUNFUNDED_OFFER`); the optional cancel-sequence is valid; the offer
     *  has not already expired; the account is authorized to receive
     *  `sfTakerPays` (via `checkAcceptAsset`); and — for domain offers — the
     *  account is a member of the referenced `PermissionedDEX` domain.
     *
     *  @param ctx Preclaim context supplying the read-only ledger view.
     *  @return `tesSUCCESS` on success, or an appropriate `ter*`/`tec*` code.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Execute the offer: cancel, cross, and optionally place on the book.
     *
     *  Allocates two sandboxes and delegates to `applyGuts`. Commits the
     *  primary sandbox when the offer is placed or fully crossed; commits only
     *  the cancel sandbox when a Fill-or-Kill offer cannot be filled (so that
     *  stale-offer housekeeping from crossing is still recorded).
     *
     *  @return `tesSUCCESS` or a `tec*` code; never `tem*`/`tef*`.
     */
    TER
    doApply() override;

    /** Accumulate per-SLE state for offer-specific invariant checks.
     *
     *  Called once per modified ledger entry after `doApply` completes. No
     *  transaction-specific invariants are currently enforced here.
     *
     *  @param isDelete `true` if the entry is being removed from the ledger.
     *  @param before SLE state before the transaction; `nullptr` for new entries.
     *  @param after  SLE state after the transaction; `nullptr` for deletions.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Finalize offer-specific invariant checks after all entries are visited.
     *
     *  No transaction-specific invariants are currently enforced. Always
     *  returns `true`.
     *
     *  @param tx     The transaction being applied.
     *  @param result The TER produced by `doApply`.
     *  @param fee    The fee charged for this transaction.
     *  @param view   The read-only ledger view after application.
     *  @param j      Journal for diagnostic logging.
     *  @return `true` — no invariant violations detected.
     */
    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;

private:
    /** Execute the core offer logic against a pair of sandboxes.
     *
     *  Handles cancellation of any existing offer nominated by
     *  `sfOfferSequence`, tick-size rounding, crossing via `flowCross`,
     *  Fill-or-Kill / Immediate-or-Cancel evaluation, reserve checks, and
     *  placement of any residual offer into the order-book directory. If the
     *  transaction should be committed with full state changes, returns
     *  `{TER, true}`; if only `viewCancel` should be committed (e.g., a
     *  killed Fill-or-Kill), returns `{TER, false}`.
     *
     *  @param view       Primary sandbox for the complete transaction result.
     *  @param viewCancel Secondary sandbox committed on Fill-or-Kill failure,
     *      preserving stale-offer removals made during crossing.
     *  @return Pair of `{TER, bool}` where the bool selects which sandbox to
     *      commit.
     */
    std::pair<TER, bool>
    applyGuts(Sandbox& view, Sandbox& viewCancel);

    /** Verify that the account is permitted to receive the specified asset.
     *
     *  Only meaningful for custom currencies (asserts XRP is never passed).
     *  For IOU assets: checks issuer existence and, when `lsfRequireAuth` is
     *  set on the issuer, verifies that a trust line exists and carries the
     *  appropriate authorization flag (`lsfLowAuth` or `lsfHighAuth` per
     *  canonical account ordering). Also rejects deep-frozen trust lines
     *  (`lsfLowDeepFreeze`/`lsfHighDeepFreeze`). For MPT assets: delegates to
     *  `requireAuth` with `WeakAuth` semantics (an `MPToken` holder entry need
     *  not pre-exist).
     *
     *  @param view   Read-only ledger view.
     *  @param flags  Apply flags (controls `ter*` vs `tec*` error selection).
     *  @param id     Account that would receive the asset.
     *  @param j      Journal for diagnostic logging.
     *  @param asset  The IOU or MPT asset to be received.
     *  @return `tesSUCCESS`, or `ter*`/`tec*` if the account lacks authority.
     */
    static TER
    checkAcceptAsset(
        ReadView const& view,
        ApplyFlags const flags,
        AccountID const id,
        beast::Journal const j,
        Asset const& asset);

    /** Cross the offer against resting orders using the payment flow engine.
     *
     *  Delegates to the same `flow()` function used by `Payment` transactions,
     *  inverting `TakerPays`/`TakerGets` so the offer creator acts as a taker.
     *  Constructs a quality threshold to enforce the passive flag; for
     *  IOU-to-IOU offers injects an XRP intermediate path to enable two-book
     *  crossing. For `tfSell` passes maximum delivery limits to accept any
     *  amount of the `Gets` asset. After crossing, computes the residual offer
     *  amounts at the original quality, factoring out gateway transfer rates.
     *  Stale offers encountered during crossing are removed in both sandboxes.
     *
     *  @param psb       Primary `PaymentSandbox` (wraps the main `Sandbox`).
     *  @param psbCancel Cancel-only `PaymentSandbox` (wraps `sbCancel`).
     *  @param takerAmount Inverted offer amounts from the taker's perspective.
     *  @param domainID  Optional permissioned-DEX domain to restrict crossing.
     *  @return Pair of `{TER, Amounts}` where `Amounts` is the residual offer
     *      after crossing (zero if fully filled).
     */
    std::pair<TER, Amounts>
    flowCross(
        PaymentSandbox& psb,
        PaymentSandbox& psbCancel,
        Amounts const& takerAmount,
        std::optional<uint256> const& domainID);

    /** Format an `STAmount` as a human-readable string for logging. */
    static std::string
    formatAmount(STAmount const& amount);

    /** Index a hybrid domain offer into the open (non-domain) order book.
     *
     *  Sets `lsfHybrid` on the offer SLE, creates a second book-directory
     *  entry using `std::nullopt` as the domain, and records that entry's
     *  key and page node in `sfAdditionalBooks` on the offer. This dual
     *  indexing lets open-market order-book walks find and consume domain
     *  offers. Only called when `tfHybrid` is set.
     *
     *  @param sb         Main sandbox holding the offer SLE.
     *  @param sleOffer   The offer ledger entry to be dually indexed.
     *  @param offerIndex Keylet of the offer entry.
     *  @param saTakerPays Amount the taker pays (book sort key).
     *  @param saTakerGets Amount the taker gets (book sort key).
     *  @param setDir     Callback that writes the book-directory key and
     *      domain into the offer SLE.
     *  @return `tesSUCCESS`, or `tecDIR_FULL` if the open book directory is
     *      at capacity.
     */
    TER
    applyHybrid(
        Sandbox& sb,
        std::shared_ptr<STLedgerEntry> sleOffer,
        Keylet const& offerIndex,
        STAmount const& saTakerPays,
        STAmount const& saTakerGets,
        std::function<void(SLE::ref, std::optional<uint256>)> const& setDir);
};

}  // namespace xrpl
