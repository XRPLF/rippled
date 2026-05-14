#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for the `NFTokenModify` transaction type.
 *
 *  Updates the URI metadata of an existing, mutable NFT on the XRP Ledger.
 *  Mutability is a permanent, at-mint choice encoded directly in the 256-bit
 *  token ID via `nft::kFLAG_MUTABLE`; tokens minted without that flag cannot
 *  be modified under any circumstances.
 *
 *  Both the original issuer and a designated authorized minter (the account
 *  recorded in the issuer's `sfNFTokenMinter` field) may submit this
 *  transaction. Any other account receives `tecNO_PERMISSION`.
 *
 *  @note `kCONSEQUENCES_FACTORY` is `Normal`; URI modification imposes no
 *      extraordinary sequencing constraints on other transactions from the
 *      same account.
 */
class NFTokenModify : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit NFTokenModify(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Stateless validation of `NFTokenModify` transaction fields.
     *
     *  Performs two pure-data checks without consulting ledger state:
     *  - If `sfOwner` is present, it must not equal `sfAccount`. The field is
     *    only meaningful when a designated minter is acting on behalf of the
     *    actual owner; a self-referential value is malformed.
     *  - If `sfURI` is present, it must be non-empty and no longer than
     *    `kMAX_TOKEN_URI_LENGTH` (256 bytes). A present-but-empty URI is
     *    rejected as ambiguous. Omitting `sfURI` entirely is valid and signals
     *    that the URI should not be changed.
     *
     *  @param ctx  Preflight context; no ledger access is performed.
     *  @return `tesSUCCESS` if all fields are valid; `temMALFORMED` if
     *      `sfOwner` equals `sfAccount`, or if `sfURI` is present but empty
     *      or exceeds the maximum length.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Read-only ledger validation for the `NFTokenModify` transaction.
     *
     *  Resolves the effective owner (`sfOwner` if present, otherwise
     *  `sfAccount`) and then performs the following checks in order:
     *  1. The NFT identified by `sfNFTokenID` must exist in the owner's token
     *     directory; absent tokens return `tecNO_ENTRY`.
     *  2. The NFT's `nft::kFLAG_MUTABLE` bit (encoded in the token ID) must be
     *     set; immutable tokens return `tecNO_PERMISSION`.
     *  3. If the signing account is not the original issuer (extracted from the
     *     token ID via `nft::getIssuer`), the issuer's `AccountRoot` must have
     *     `sfNFTokenMinter` pointing to the signing account; any other account
     *     returns `tecNO_PERMISSION`.
     *
     *  @param ctx  Preclaim context providing read-only ledger access.
     *  @return `tesSUCCESS` if all checks pass; `tecNO_ENTRY` if the NFT does
     *      not exist; `tecNO_PERMISSION` if the token is immutable or the
     *      caller is neither the issuer nor the designated minter;
     *      `tecINTERNAL` if the issuer's `AccountRoot` cannot be read (ledger
     *      corruption sentinel, excluded from coverage).
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Apply the `NFTokenModify` transaction to the mutable ledger view.
     *
     *  Resolves the effective owner (`sfOwner` if present, otherwise
     *  `sfAccount`) and delegates all ledger writes to
     *  `nft::changeTokenURI()`, which locates the correct `NFTokenPage` SLE
     *  and updates the stored URI in place. Passing `ctx_.tx[~sfURI]` (an
     *  optional) allows the helper to distinguish an explicit URI update from
     *  an omitted field.
     *
     *  @return The `TER` result from `nft::changeTokenURI()`, which is
     *      `tesSUCCESS` on a successful write or an error code if the page
     *      cannot be located.
     */
    TER
    doApply() override;

    /** Per-entry invariant visitor for `NFTokenModify`.
     *
     *  No transaction-specific invariants are currently enforced; the override
     *  is a placeholder for future work.
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

    /** Post-apply invariant finalizer for `NFTokenModify`.
     *
     *  No transaction-specific invariants are currently enforced; always
     *  returns `true`. The override is a placeholder for future work.
     *
     *  @param tx      The transaction being applied.
     *  @param result  The TER result from `doApply()`.
     *  @param fee     The fee charged for this transaction.
     *  @param view    Read-only view of the ledger after apply.
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
