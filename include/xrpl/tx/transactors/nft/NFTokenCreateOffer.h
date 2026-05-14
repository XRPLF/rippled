#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/**
 * Transactor for `ttNFTOKEN_CREATE_OFFER` (type code 27).
 *
 * Places a buy or sell offer for an NFT into the ledger. When `tfSellNFToken`
 * is set the submitter is offering to sell an NFT they own; when unset they
 * are offering to buy an NFT currently held by the account named in
 * `sfOwner`. The distinction drives which account's NFT directory is searched
 * in `preclaim` and which reserve is charged in `doApply`.
 *
 * All three pipeline phases delegate almost entirely to free functions in
 * `NFTokenHelpers` (`tokenOfferCreatePreflight`, `tokenOfferCreatePreclaim`,
 * `tokenOfferCreateApply`) that are shared with `NFTokenMint`, which can
 * create a sell offer atomically at mint time. This sharing ensures the two
 * transactors cannot drift apart on offer-creation rules.
 *
 * @note `ConsequencesFactory{Normal}` means a fee is charged on failure; this
 *     transaction does not block later transactions from the same account.
 */
class NFTokenCreateOffer : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit NFTokenCreateOffer(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /**
     * Returns the flag mask for this transaction type (`tfNFTokenCreateOfferMask`).
     *
     * The framework passes this mask to `preflight1`, which rejects any
     * transaction whose flags include bits not defined for
     * `ttNFTOKEN_CREATE_OFFER`. This guards against clients setting
     * unrecognized bits that could acquire meaning under future amendments.
     */
    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    /**
     * Stateless structural validation — no ledger access.
     *
     * Extracts NFT flags embedded in `sfNFTokenID` (e.g. `lsfBurnable`,
     * `lsfOnlyXRP`, `lsfTransferable`) then delegates to
     * `nft::tokenOfferCreatePreflight()`, which validates the offer amount,
     * optional destination and expiration, ownership rules, and transaction
     * flags.
     *
     * @return `tesSUCCESS` if the transaction is structurally valid; a
     *     `tem*` code otherwise.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /**
     * Read-only ledger checks after signature verification.
     *
     * Performs two checks before delegating to the shared helper:
     *
     * 1. **Expiration guard** — rejects with `tecEXPIRED` if the offer's
     *    `sfExpiration` has already passed the current ledger close time.
     *    This cannot be done in `preflight` because close time is ledger
     *    state.
     * 2. **Token ownership** — for sell offers, confirms `sfAccount` holds
     *    the NFT; for buy offers, confirms `sfOwner` holds it. Returns
     *    `tecNO_ENTRY` if the token is absent from the expected directory.
     *
     * After these two checks, delegates to
     * `nft::tokenOfferCreatePreclaim()` for business-logic validation
     * (transfer fee eligibility, destination account existence, etc.).
     *
     * @return `tesSUCCESS` on success; `tecEXPIRED`, `tecNO_ENTRY`, or
     *     another `tec*`/`ter*` code on failure.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /**
     * Creates the `NFTokenOffer` ledger object and inserts it into the
     * appropriate buy or sell directory.
     *
     * Delegates entirely to `nft::tokenOfferCreateApply()`, passing all
     * transaction fields plus `preFeeBalance_` (the submitter's XRP balance
     * before fee deduction) for reserve checking.
     *
     * @return `tesSUCCESS` on success; a `tec*` code if the offer object
     *     cannot be created (e.g. `tecINSUFFICIENT_RESERVE`).
     */
    TER
    doApply() override;

    /**
     * Per-entry invariant visitor hook.
     *
     * No transaction-specific invariants are currently enforced; this is a
     * no-op placeholder for future work.
     *
     * @param isDelete `true` if the entry is being deleted.
     * @param before    The SLE state before the transaction, or `nullptr`.
     * @param after     The SLE state after the transaction, or `nullptr`.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /**
     * Post-execution invariant finalizer.
     *
     * No transaction-specific invariants are currently enforced; always
     * returns `true`. Placeholder for future work.
     *
     * @param tx     The transaction being applied.
     * @param result The `TER` code produced by `doApply`.
     * @param fee    The fee charged, in drops.
     * @param view   The ledger view after the transaction.
     * @param j      Journal for diagnostic logging.
     * @return `true` — all invariants pass (none are currently defined).
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
