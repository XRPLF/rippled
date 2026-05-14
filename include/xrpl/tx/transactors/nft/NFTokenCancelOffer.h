#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for the `NFTokenCancelOffer` transaction type.
 *
 *  Removes one or more outstanding `NFTokenOffer` ledger objects that were
 *  created via `NFTokenCreateOffer`. Up to `kMAX_TOKEN_OFFER_CANCEL_COUNT`
 *  (500) offers may be cancelled in a single transaction. The operation is
 *  idempotent with respect to already-consumed or already-cancelled offers:
 *  missing IDs are silently skipped in both `preclaim` and `doApply`.
 *
 *  Permission to cancel an offer is deliberately broad: the offer's creator,
 *  its designated destination (if any), and any account once the offer has
 *  expired are all entitled to cancel. This reflects the NFT design goal of
 *  making expired and directed offers easy to clean up without requiring the
 *  original creator to be online.
 */
class NFTokenCancelOffer : public Transactor
{
public:
    /** Standard consequences factory tag.
     *
     *  `Normal` indicates this transaction has ordinary reserve consequences
     *  and does not block later transactions from the same account.
     */
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit NFTokenCancelOffer(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Stateless structural validation of the offer-ID list.
     *
     *  Checks two invariants without consulting ledger state:
     *  - The `sfNFTokenOffers` field must be non-empty and must not exceed
     *    `kMAX_TOKEN_OFFER_CANCEL_COUNT` (500) entries. An empty list has no
     *    meaningful effect; an oversized list is rejected as a DoS vector.
     *  - The list must contain no duplicate IDs. Duplicates are detected by
     *    sorting a local copy and scanning for adjacent equal elements via
     *    `std::adjacent_find`. A copy is used because the original field
     *    object is conceptually immutable at this stage.
     *
     *  @param ctx The preflight context carrying the transaction and rules.
     *  @return `temMALFORMED` if the list is empty, exceeds the maximum, or
     *      contains duplicates; `tesSUCCESS` otherwise.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Verifies that the submitting account may cancel every listed offer.
     *
     *  Iterates `sfNFTokenOffers` and short-circuits on the first entry that
     *  the submitter is not entitled to cancel. For each ID:
     *  - If the offer no longer exists in the ledger it is silently skipped
     *    (idempotent with respect to already-consumed or already-cancelled
     *    offers).
     *  - If the ID resolves to a ledger object that is not an
     *    `ltNFTOKEN_OFFER`, the submitter has no permission regardless of
     *    account ownership.
     *  - If the offer has expired (`hasExpired` returns true), any account
     *    may cancel it; the check returns allowed immediately.
     *  - The offer's `sfOwner` or its optional `sfDestination` (if it matches
     *    the submitting account) grants cancellation rights.
     *
     *  @param ctx The preclaim context providing a read-only ledger view.
     *  @return `tecNO_PERMISSION` if any listed offer exists, is the wrong
     *      type, has not expired, and belongs neither to the submitter nor
     *      names the submitter as its destination; `tesSUCCESS` otherwise.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Deletes each listed `NFTokenOffer` from the ledger.
     *
     *  Iterates `sfNFTokenOffers` and calls `nft::deleteTokenOffer` on each
     *  entry that still exists (via `keylet::nftoffer`). Missing offers are
     *  skipped silently, consistent with the idempotency stance of `preclaim`.
     *  If deletion fails for a present offer — a situation deemed impossible
     *  under correct invariants and excluded from code coverage — a fatal log
     *  message is emitted and `tefBAD_LEDGER` is returned to signal internal
     *  ledger corruption rather than a user error.
     *
     *  @return `tesSUCCESS` on success; `tefBAD_LEDGER` if `deleteTokenOffer`
     *      returns false for a live offer (indicates ledger corruption).
     */
    TER
    doApply() override;

    /** Per-entry invariant hook (no-op; reserved for future use). */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Invariant finalization hook (no-op; reserved for future use).
     *
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
