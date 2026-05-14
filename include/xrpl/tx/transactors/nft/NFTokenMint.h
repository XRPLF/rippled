#pragma once

#include <xrpl/ledger/helpers/NFTokenHelpers.h>
#include <xrpl/protocol/nft.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for the `NFTokenMint` transaction type.
 *
 *  Creates a new non-fungible token on the XRP Ledger and records it in the
 *  issuer's on-ledger NFToken page structure. Optionally, in the same atomic
 *  step, it can create an initial sell offer for the newly minted token when
 *  `featureNFTokenMintOffer` is active and the transaction includes offer
 *  fields (`sfAmount`, optionally `sfDestination` and `sfExpiration`).
 *
 *  The offer-creation helpers (`nft::tokenOfferCreatePreflight`,
 *  `nft::tokenOfferCreatePreclaim`, `nft::tokenOfferCreateApply`) are shared
 *  with `NFTokenCreateOffer` to keep offer-validation logic consistent whether
 *  an offer is created standalone or bundled into a mint. The embedded-offer
 *  path always creates a sell offer — atomic mint-and-buy-offer is not
 *  supported.
 *
 *  @note `kCONSEQUENCES_FACTORY` is `Normal`; minting imposes no extraordinary
 *      sequencing constraints on other transactions from the same account.
 */
class NFTokenMint : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit NFTokenMint(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Gate the embedded-offer sub-feature during preflight.
     *
     *  Returns `false` (blocking the transaction) if the transaction contains
     *  any of `sfAmount`, `sfDestination`, or `sfExpiration` — the fields used
     *  to bundle an initial sell offer — and `featureNFTokenMintOffer` is not
     *  enabled on the current network. Without this guard a mint-and-offer
     *  transaction could activate on a network that has not voted for the
     *  combined flow.
     *
     *  @param ctx  Preflight context; no ledger access is performed.
     *  @return `true` if the transaction is compatible with active amendments;
     *      `false` if offer fields are present but `featureNFTokenMintOffer`
     *      is not yet enabled.
     */
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    /** Return the valid transaction-flag bitmask for the current amendment set.
     *
     *  The set of legal flags is amendment-sensitive in two ways:
     *  - `tfTrustLine` was permanently disabled by `fixRemoveNFTokenAutoTrustLine`
     *    to close a denial-of-service vector: two cooperating accounts could trade
     *    an NFT in a loop, each transfer creating a new trustline on the issuer
     *    and growing the issuer's reserve without bound.
     *  - `tfMutable` (mutable-URI NFTs) is added by `featureDynamicNFT`.
     *
     *  The returned mask is one of four combinations depending on which of these
     *  two amendments are active.
     *
     *  @param ctx  Preflight context used to query active amendments.
     *  @return A bitmask of `tf*` flags that are legal for this transaction.
     */
    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    /** Stateless validation of `NFTokenMint` transaction fields.
     *
     *  Checks mint-specific field constraints:
     *  - `sfTransferFee` must not exceed `maxTransferFee`; if non-zero,
     *    `tfTransferable` must also be set (a transfer fee on a non-transferable
     *    token is contradictory).
     *  - `sfIssuer`, when present, must not equal `sfAccount` — the
     *    authorized-minter pattern requires distinct minter and issuer accounts.
     *  - `sfURI`, when present, must be non-empty and within `maxTokenURILength`.
     *  - If offer fields are present, `sfAmount` is mandatory; offer field
     *    validity is then delegated to `nft::tokenOfferCreatePreflight()`.
     *
     *  @param ctx  Preflight context; no ledger access is performed.
     *  @return `tesSUCCESS` if all fields are valid; a `tem*` code describing
     *      the first constraint violated.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Read-only ledger validation for the `NFTokenMint` transaction.
     *
     *  When `sfIssuer` is present (authorized-minter path), reads the issuer's
     *  `AccountRoot` and verifies that its `sfNFTokenMinter` field matches the
     *  signing account. A missing issuer account returns `tecNO_ISSUER`; a
     *  mismatch returns `tecNO_PERMISSION`.
     *
     *  If offer fields are present, also invokes `nft::tokenOfferCreatePreclaim()`
     *  to check offer-specific ledger conditions (expiry, trustline authorisation,
     *  deep-freeze status, etc.).
     *
     *  @param ctx  Preclaim context providing read-only ledger access.
     *  @return `tesSUCCESS` if all checks pass; `tecNO_ISSUER` if the issuer
     *      account does not exist; `tecNO_PERMISSION` if the signing account is
     *      not the designated minter; or any error propagated from
     *      `nft::tokenOfferCreatePreclaim()`.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Apply the `NFTokenMint` transaction to the mutable ledger view.
     *
     *  Execution proceeds in up to four steps:
     *  1. **Sequence bookkeeping**: On the issuer's `AccountRoot`,
     *     `sfFirstNFTokenSequence` is seeded on the first mint (set to the
     *     issuer's current account sequence, decremented by one when the issuer
     *     submits the transaction directly with a sequence number because the
     *     sequence has already been pre-incremented by this point).
     *     `sfMintedNFTokens` is incremented; overflow returns
     *     `tecMAX_SEQUENCE_REACHED`.
     *  2. **Token insertion**: The NFToken `STObject` is assembled with the
     *     computed ID and optional URI, then placed into the issuer's NFToken
     *     page structure via `nft::insertToken()`.
     *  3. **Optional sell offer**: If `sfAmount` is present,
     *     `nft::tokenOfferCreateApply()` creates a sell offer atomically.
     *  4. **Reserve check**: Performed only when the owner count increased
     *     relative to its pre-mint value — packing NFTs into an existing page
     *     does not raise the owner count and therefore does not require a
     *     reserve top-up.
     *
     *  @return `tesSUCCESS` on success; `tecMAX_SEQUENCE_REACHED` if the
     *      issuer's NFT sequence counter would overflow; `tecINSUFFICIENT_RESERVE`
     *      if a new page or sell offer was created and the issuer cannot cover
     *      the increased reserve; or any error from `nft::insertToken()` or
     *      `nft::tokenOfferCreateApply()`.
     */
    TER
    doApply() override;

    /** Per-entry invariant visitor for `NFTokenMint`.
     *
     *  Tracks NFToken page and NFT issuance count changes to verify that
     *  minting results in exactly the expected ledger mutations.
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

    /** Post-apply invariant finalizer for `NFTokenMint`.
     *
     *  Verifies that the net NFT count change across all visited entries is
     *  consistent with a successful mint (+1 net token created).
     *
     *  @param tx      The transaction being applied.
     *  @param result  The TER result from `doApply()`.
     *  @param fee     The fee charged for this transaction.
     *  @param view    Read-only view of the ledger after apply.
     *  @param j       Journal for diagnostic logging.
     *  @return `true` if the invariant holds; `false` if an unexpected NFT
     *      count discrepancy is detected.
     */
    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;

    /** Construct the 32-byte big-endian NFToken ID from its component fields.
     *
     *  The layout is:
     *
     *  | Bytes | Content                          |
     *  |-------|----------------------------------|
     *  | 0–1   | Flags (2 bytes, big-endian)      |
     *  | 2–3   | Transfer fee (2 bytes, big-endian)|
     *  | 4–23  | Issuer `AccountID` (20 bytes)    |
     *  | 24–27 | Ciphered taxon (4 bytes, big-endian) |
     *  | 28–31 | Token sequence number (4 bytes, big-endian) |
     *
     *  The taxon is scrambled via `nft::cipheredTaxon()` before packing:
     *  `taxon ^ ((384160001 * tokenSeq) + 2459)`. This linear congruential
     *  transform — a permutation of the 32-bit integer space by the Hull-Dobell
     *  theorem — distributes tokens with the same taxon across different NFToken
     *  pages, avoiding page hot-spots that would degrade lookup and deletion
     *  performance. The constants are **protocol-frozen**: changing them would
     *  corrupt interpretation of existing token IDs and require a new amendment.
     *
     *  Exposed publicly to enable unit testing without a full transaction context.
     *
     *  @param flags     Two-byte flag field (e.g., `nft::kFLAG_TRANSFERABLE`).
     *  @param fee       Transfer fee in units of 1/100,000 of a percent (0–50,000).
     *  @param issuer    The issuer's 20-byte `AccountID`.
     *  @param taxon     The unscrambled taxon value assigned by the issuer.
     *  @param tokenSeq  The per-issuer mint sequence number used as the cipher key
     *                   and stored in the final 4 bytes of the ID.
     *  @return A `uint256` token ID encoding all five fields.
     */
    static uint256
    createNFTokenID(
        std::uint16_t flags,
        std::uint16_t fee,
        AccountID const& issuer,
        nft::Taxon taxon,
        std::uint32_t tokenSeq);
};

}  // namespace xrpl
