#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for the `OracleSet` transaction type (XLS-47d).
 *
 *  Handles both creation and in-place update of Price Oracle ledger objects.
 *  A Price Oracle bridges off-chain data sources (e.g., exchange price feeds)
 *  and on-ledger consumers by publishing timestamped token-pair prices under
 *  an `(account, sfOracleDocumentID)` key. The counterpart transactor,
 *  `OracleDelete`, handles removal.
 *
 *  @note Owner-count reserve follows a tiered model: oracles with more than
 *      five `sfPriceDataSeries` entries consume **two** reserve slots; all
 *      others consume one. This threshold is shared with `OracleDelete`.
 */
class OracleSet : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit OracleSet(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Stateless structural validation (no ledger access).
     *
     *  Checks that `sfPriceDataSeries` is non-empty and within
     *  `kMAX_ORACLE_DATA_SERIES`, and that the optional string fields
     *  `sfProvider`, `sfURI`, and `sfAssetClass` — when present — are
     *  non-empty and within their respective maximum lengths.
     *
     *  Deeper token-pair consistency is deferred to `preclaim`, which can
     *  compare against the existing oracle SLE.
     *
     *  @param ctx Preflight context (no ledger access).
     *  @return `temARRAY_EMPTY` if `sfPriceDataSeries` is empty;
     *      `temARRAY_TOO_LARGE` if it exceeds `kMAX_ORACLE_DATA_SERIES`;
     *      `temMALFORMED` if any optional string field violates its length
     *          bounds; `tesSUCCESS` otherwise.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Read-only ledger validation; distinguishes create from update.
     *
     *  **Timestamp freshness.** `sfLastUpdateTime` is stored in XRPL epoch
     *  seconds (offset from Unix epoch by `kEPOCH_OFFSET`). The converted
     *  Unix timestamp must lie within `±kMAX_LAST_UPDATE_TIME_DELTA` of the
     *  ledger's `closeTime`. On update it must also be strictly greater than
     *  the existing oracle's `sfLastUpdateTime`.
     *
     *  **Create.** If no oracle SLE exists for `(account, sfOracleDocumentID)`,
     *  `sfProvider` and `sfAssetClass` are mandatory, and any entry in
     *  `sfPriceDataSeries` that lacks `sfAssetPrice` is malformed (there is
     *  nothing to delete from a non-existent object).
     *
     *  **Update.** If the oracle SLE exists, `sfProvider` and `sfAssetClass`
     *  may be omitted; if supplied they must match the stored values (these
     *  fields are immutable after creation). Entries without `sfAssetPrice`
     *  signal deletion; a deletion request for a pair not in the current
     *  oracle returns `tecTOKEN_PAIR_NOT_FOUND`. Duplicate pairs within a
     *  single transaction are rejected.
     *
     *  **Reserve.** The account balance is checked against the reserve
     *  required by the resulting pair count (using the tiered 1-or-2-slot
     *  model) before the fee is deducted.
     *
     *  @param ctx Preclaim context (read-only ledger access).
     *  @return `tesSUCCESS` on success;
     *      `terNO_ACCOUNT` if the submitting account does not exist;
     *      `tecINVALID_UPDATE_TIME` if the timestamp is out of the freshness
     *          window or is not strictly newer than the existing oracle's
     *          timestamp on update;
     *      `tecTOKEN_PAIR_NOT_FOUND` if a deletion targets a pair absent from
     *          the current oracle;
     *      `tecARRAY_EMPTY` if the resolved pair set after merging is empty;
     *      `tecARRAY_TOO_LARGE` if the resolved pair set exceeds
     *          `kMAX_ORACLE_DATA_SERIES`;
     *      `tecINSUFFICIENT_RESERVE` if the account cannot cover the reserve;
     *      `temMALFORMED` for structural violations (duplicate pairs,
     *          self-referential base/quote, mismatched immutable fields, or
     *          a deletion entry in a creation context).
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Apply the oracle create or update to the mutable ledger view.
     *
     *  **Update path.** Loads all existing pairs into a
     *  `std::map<pair<Currency,Currency>, STObject>` keyed by `tokenPairKey`,
     *  applies the transaction's entries (delete / update-in-place / insert),
     *  and serialises the result back as `sfPriceDataSeries`. Owner-count is
     *  adjusted for any tier change. `sfURI` and `sfLastUpdateTime` are
     *  updated unconditionally. Under `fixIncludeKeyletFields`, `sfOracleDocumentID`
     *  is back-filled onto older objects that predate the amendment.
     *
     *  **Create path.** Constructs a new SLE, inserts it into the owner
     *  directory, and increments the owner count by 1 or 2 per the tiered
     *  model. Under `fixPriceOracleOrder`, the initial `sfPriceDataSeries`
     *  is sorted via the same map approach as the update path; without the
     *  amendment, raw transaction order is preserved (legacy behaviour).
     *
     *  @return `tesSUCCESS` on success;
     *      `tecDIR_FULL` if the owner directory is full (create path only);
     *      `tefINTERNAL` if the account SLE cannot be peeked for owner-count
     *          adjustment (unreachable under normal conditions).
     */
    TER
    doApply() override;

    /** Per-entry invariant visitor (no-op; reserved for future work). */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Post-transaction invariant check (no-op; reserved for future work).
     *
     *  @return `true` unconditionally.
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
