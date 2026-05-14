#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for the `NFTokenBurn` transaction type.
 *
 *  Permanently destroys a single NFToken: removes it from its owner's
 *  NFTokenPage, increments the issuer's `sfBurnedNFTokens` counter, and
 *  deletes associated buy/sell offers up to `kMAX_DELETABLE_TOKEN_OFFER_ENTRIES`
 *  (500 total). Offers beyond that cap are not removed by this transaction.
 *
 *  @note `kCONSEQUENCES_FACTORY` is `Normal`; burn imposes no extraordinary
 *      sequencing constraints on other transactions from the same account.
 */
class NFTokenBurn : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit NFTokenBurn(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Stateless validation of the `NFTokenBurn` transaction fields.
     *
     *  All generic field checks (fee, sequence, signing key format) are handled
     *  by `preflight1` and `preflight2` inside `invokePreflight`. There are no
     *  burn-specific stateless constraints, so this always returns `tesSUCCESS`.
     *
     *  @param ctx  Preflight context; no ledger access is performed.
     *  @return `tesSUCCESS` unconditionally.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Read-only ledger validation for the `NFTokenBurn` transaction.
     *
     *  Performs two checks:
     *  1. **Token existence**: `nft::findToken` searches the owner's NFTokenPage
     *     for `sfNFTokenID`. Returns `tecNO_ENTRY` if the token is not found.
     *  2. **Burn permission**: the token owner may always burn their own token.
     *     If `sfAccount` differs from the owner (`sfOwner` field when present),
     *     the token's `nft::kFLAG_BURNABLE` bit must be set; otherwise
     *     `tecNO_PERMISSION` is returned. Even with the flag set, only the
     *     issuer — or the account designated as the issuer's `sfNFTokenMinter`
     *     — may burn a token they do not hold.
     *
     *  @param ctx  Preclaim context providing read-only ledger access.
     *  @return `tesSUCCESS` if all checks pass; `tecNO_ENTRY` if the token does
     *      not exist; `tecNO_PERMISSION` if the submitter is not authorised to
     *      burn the token.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Apply the `NFTokenBurn` transaction to the mutable ledger view.
     *
     *  Execution proceeds in three steps:
     *  1. **Token removal**: `nft::removeToken` strips the NFToken from its
     *     owner's NFTokenPage SLE. A post-condition check guards against the
     *     token having disappeared since `preclaim` (ledger corruption sentinel;
     *     normally unreachable under consensus).
     *  2. **Issuer accounting**: the issuer's `sfBurnedNFTokens` counter is
     *     incremented (seeding from zero via `value_or(0)` on first burn).
     *  3. **Offer cleanup**: sell offers are deleted first (their directory is
     *     typically smaller), then any remaining budget from the 500-offer cap
     *     is applied to buy offers. Offers beyond the cap are left in the ledger.
     *
     *  @return `tesSUCCESS` on success; propagates any non-success `TER` from
     *      `nft::removeToken` (normally unreachable after a successful `preclaim`).
     */
    TER
    doApply() override;

    /** Per-entry invariant visitor; reserved for future transaction-specific invariants.
     *
     *  @param isDelete  True if the entry is being deleted.
     *  @param before    SLE state before the transaction (null for insertions).
     *  @param after     SLE state after the transaction (null for deletions).
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Post-apply invariant finalizer; reserved for future transaction-specific invariants.
     *
     *  Currently a no-op that always returns `true`.
     *
     *  @param tx      The transaction being applied.
     *  @param result  The TER result from `doApply()`.
     *  @param fee     The fee charged for this transaction.
     *  @param view    Read-only view of the ledger after apply.
     *  @param j       Journal for diagnostic logging.
     *  @return `true` (no invariants checked yet).
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
