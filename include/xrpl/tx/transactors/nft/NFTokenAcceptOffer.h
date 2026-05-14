#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/**
 * Transactor for the `NFTokenAcceptOffer` transaction type.
 *
 * Settles an NFT trade by transferring ownership of a token and routing payment
 * among up to four parties: buyer, seller, issuer (royalty), and an optional broker.
 *
 * Two mutually exclusive operation modes are supported:
 *
 * - **Direct mode**: exactly one of `sfNFTokenBuyOffer` or `sfNFTokenSellOffer` is
 *   supplied. The submitter is either the current NFT owner accepting a buy offer,
 *   or a buyer paying into a sell offer. Payment routing is handled by `acceptOffer()`.
 *
 * - **Brokered mode**: both offer IDs are supplied. The submitter is a broker who
 *   matches the buyer's bid against the seller's ask without owning the token. An
 *   optional `sfNFTokenBrokerFee` lets the broker claim a cut. Payment sequencing
 *   in this mode is enforced in `doApply()`: broker fee first, then issuer royalty
 *   computed on the remainder, then seller receives what is left. This ordering
 *   prevents total payouts from exceeding what the buyer authorised.
 *
 * @note `ConsequencesFactory` is `Normal`; this transaction imposes no extraordinary
 *     sequencing constraints on other transactions in the same ledger.
 */
class NFTokenAcceptOffer : public Transactor
{
private:
    /**
     * Transfer `amount` from `from` to `to` and verify both balances remain non-negative.
     *
     * Wraps `accountSend` with a post-payment sanity check: a successful `accountSend`
     * can still leave a balance negative in pathological IOU-with-transfer-fee scenarios.
     * If either sender or receiver ends up with a negative effective balance (as reported
     * by `accountFunds`), the method returns `tecINSUFFICIENT_FUNDS` before any ledger
     * write is committed.
     *
     * @param from  Account debited.
     * @param to    Account credited.
     * @param amount  Amount to transfer; must be non-negative.
     * @return `tesSUCCESS` on success, or a `tec*` code if the transfer fails or
     *     leaves either party with a negative balance.
     */
    TER
    pay(AccountID const& from, AccountID const& to, STAmount const& amount);

    /**
     * Execute a direct (non-brokered) offer acceptance.
     *
     * Determines buyer and seller roles from the offer's `lsfSellNFToken` flag, then:
     * 1. Computes the issuer's royalty cut via `nft::getTransferFee`; skips royalty if
     *    buyer or seller is the issuer.
     * 2. Pays the royalty to the issuer via `pay()`.
     * 3. Pays the remaining proceeds to the seller via `pay()`.
     * 4. Transfers the token from seller to buyer via `transferNFToken()`.
     *
     * @param offer  The active sell or buy offer SLE being accepted.
     * @return `tesSUCCESS` on success, or a `tec*` code propagated from `pay()` or
     *     `transferNFToken()`.
     */
    TER
    acceptOffer(std::shared_ptr<SLE> const& offer);

    /**
     * Execute a brokered offer acceptance, matching `buy` against `sell`.
     *
     * @note This overload is not used directly; brokered-mode payment sequencing is
     *     implemented inline in `doApply()` to enforce the broker-first, then
     *     issuer-royalty, then seller ordering invariant.
     *
     * @param buy   The buyer's NFToken offer SLE.
     * @param sell  The seller's NFToken offer SLE.
     * @return `tesSUCCESS` on success, or a `tec*` code.
     */
    TER
    bridgeOffers(std::shared_ptr<SLE> const& buy, std::shared_ptr<SLE> const& sell);

    /**
     * Transfer ownership of an NFToken from `seller` to `buyer`.
     *
     * Calls `nft::removeToken` to strip the token from the seller's NFToken page,
     * then `nft::insertToken` to place it into the buyer's page (allocating a new
     * page if needed).
     *
     * When `fixNFTokenReserve` is enabled and the buyer's owner count increased
     * (indicating a new NFToken page was allocated), the method checks the buyer's
     * current post-purchase balance against the reserve requirement for the new owner
     * count. The current balance — not `preFeeBalance_` — is used because the buyer
     * may have already paid for the token; using the pre-fee balance would overstate
     * available funds and allow an under-reserved purchase.
     *
     * @param buyer      Account receiving the token.
     * @param seller     Account surrendering the token.
     * @param nfTokenID  The 256-bit token identifier.
     * @return `tesSUCCESS` on success; `tecINSUFFICIENT_RESERVE` if the buyer cannot
     *     meet the reserve for the new page; `tecINTERNAL` if the token cannot be
     *     located (ledger corruption sentinel, normally unreachable).
     */
    TER
    transferNFToken(AccountID const& buyer, AccountID const& seller, uint256 const& nfTokenID);

public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit NFTokenAcceptOffer(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /**
     * Stateless validation of the `NFTokenAcceptOffer` transaction fields.
     *
     * Enforces:
     * - At least one of `sfNFTokenBuyOffer` or `sfNFTokenSellOffer` must be present.
     * - `sfNFTokenBrokerFee` is only valid in brokered mode (both offer IDs present)
     *   and must be strictly positive.
     *
     * @param ctx  Preflight context; no ledger access is performed.
     * @return `tesSUCCESS` if the transaction is well-formed, `temMALFORMED` otherwise.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /**
     * Read-only ledger validation for the `NFTokenAcceptOffer` transaction.
     *
     * For each offer ID present in the transaction, verifies:
     * - The offer SLE exists and has a non-negative `sfAmount`.
     * - Expiration handling: before `fixCleanup3_1_3`, expired offers return
     *   `tecEXPIRED` immediately, leaving the ledger object stranded; after the
     *   amendment, expired offers are allowed through to `doApply()` where they are
     *   deleted before returning `tecEXPIRED`.
     *
     * In **brokered mode**, additionally confirms:
     * - Both offers reference the same token ID and the same payment asset.
     * - The buyer's bid is at least as large as the seller's ask.
     * - After deducting `sfNFTokenBrokerFee`, the seller's ask is still satisfied.
     * - Neither party is selling to themselves.
     * - The broker, buyer, and seller each have an authorised, non-deep-frozen trust
     *   line for the IOU being transferred (`fixEnforceNFTokenTrustlineV2`).
     *
     * In **direct mode**, additionally confirms:
     * - The transaction submitter owns the token (when accepting a buy offer).
     * - The buyer has sufficient funds.
     * - All payment recipients have authorised, non-deep-frozen trust lines for the
     *   IOU (`fixEnforceNFTokenTrustlineV2`).
     *
     * Royalty trust-line hygiene:
     * - `fixEnforceNFTokenTrustline`: prevents granting an unintended trust line to
     *   the issuer when no line exists for the royalty IOU.
     * - `fixEnforceNFTokenTrustlineV2`: extends the check to every payment recipient.
     *
     * @param ctx  Preclaim context providing read-only ledger access.
     * @return `tesSUCCESS` if all checks pass, or a `tec*`/`tem*` code describing
     *     the first violation found.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /**
     * Apply the `NFTokenAcceptOffer` transaction to the mutable ledger view.
     *
     * Expired offer cleanup (when `fixCleanup3_1_3` is enabled): if any referenced
     * offer has expired since `preclaim`, it is deleted from the ledger and
     * `tecEXPIRED` is returned, ensuring proper garbage collection.
     *
     * Accepted offers are deleted from the ledger regardless of whether they are buy
     * or sell offers.
     *
     * **Direct mode** (only one offer ID present): delegates to `acceptOffer()`.
     *
     * **Brokered mode** (both offer IDs present): payments are sequenced as follows to
     * preserve the correctness invariant that total payouts never exceed what the buyer
     * authorised:
     * 1. Broker receives `sfNFTokenBrokerFee` from the buyer.
     * 2. Issuer royalty is computed on the buyer's remaining amount after the broker
     *    cut; issuer is paid via `pay()`.
     * 3. Seller receives whatever balance remains after both deductions.
     * 4. NFToken ownership is transferred via `transferNFToken()`.
     *
     * @return `tesSUCCESS` on a fully committed sale; `tecEXPIRED` if an expired
     *     offer was cleaned up; other `tec*` codes propagated from `pay()` or
     *     `transferNFToken()`; `tecINTERNAL` on ledger corruption (normally unreachable).
     */
    TER
    doApply() override;

    /**
     * Per-entry invariant visitor; reserved for future transaction-specific invariants.
     *
     * @param isDelete  True if the entry is being deleted.
     * @param before    The SLE state before the transaction (may be null for insertions).
     * @param after     The SLE state after the transaction (may be null for deletions).
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /**
     * Post-apply invariant finalizer; reserved for future transaction-specific invariants.
     *
     * Currently a no-op that always returns `true`.
     *
     * @param tx      The transaction being applied.
     * @param result  The TER result from `doApply()`.
     * @param fee     The fee charged for this transaction.
     * @param view    Read-only view of the ledger after apply.
     * @param j       Journal for diagnostic logging.
     * @return `true` (no invariants checked yet).
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
