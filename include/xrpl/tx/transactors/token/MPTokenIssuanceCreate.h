#pragma once

#include <xrpl/basics/Expected.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Input parameters for creating a new MPTokenIssuance ledger object.
 *
 *  This aggregate decouples the creation logic from the transaction context so
 *  that `MPTokenIssuanceCreate::create()` can be called as a side effect by
 *  other transactors — most notably `VaultCreate`, which mints a share-token
 *  issuance when creating a single-asset vault — without constructing a full
 *  `ApplyContext`.
 *
 *  @note When `priorBalance` is present, `create()` enforces the owner-reserve
 *      requirement.  Callers that manage the reserve externally (e.g. vault
 *      pseudo-account creation) should pass `std::nullopt` to skip the check.
 */
struct MPTCreateArgs
{
    /** Account balance before fee deduction.  When set, `create()` returns
     *  `tecINSUFFICIENT_RESERVE` if the balance cannot cover the new reserve
     *  increment.  Pass `std::nullopt` to skip the reserve check. */
    std::optional<XRPAmount> priorBalance;

    /** Issuing account; combined with `sequence` to derive the deterministic
     *  `MPTID` via `makeMptID()`. */
    AccountID const& account;

    /** Transaction sequence value used with `account` to compute the `MPTID`.
     *  Stored in the issuance SLE as `sfSequence`. */
    std::uint32_t sequence = 0;

    /** Transaction flags, stripped of universal bits (`~tfUniversal`) before
     *  being written to `sfFlags` on the issuance SLE. */
    std::uint32_t flags = 0;

    /** Optional supply cap for the issuance.  Must be positive and at most
     *  `maxMPTokenAmount` (0x7FFF_FFFF_FFFF_FFFF).  Absent means no cap. */
    std::optional<std::uint64_t> maxAmount =
        std::nullopt;  // NOLINT(readability-redundant-member-init)

    /** Optional decimal-scale exponent stored in `sfAssetScale` on the SLE. */
    std::optional<std::uint8_t> assetScale =
        std::nullopt;  // NOLINT(readability-redundant-member-init)

    /** Per-transfer fee in basis points.  Must be ≤ `maxTransferFee`
     *  (50,000).  A non-zero value requires `tfMPTCanTransfer` to be set in
     *  `flags`. */
    std::optional<std::uint16_t> transferFee =
        std::nullopt;  // NOLINT(readability-redundant-member-init)

    /** Arbitrary binary metadata stored in `sfMPTokenMetadata`.  When
     *  present must be non-empty and at most `maxMPTokenMetadataLength`
     *  (1,024) bytes. */
    std::optional<Slice> const& metadata{};

    /** Optional permissioned-domain ID.  When set the issuance is private
     *  and `tfMPTRequireAuth` must be set in `flags`.  Requires both
     *  `featurePermissionedDomains` and `featureSingleAssetVault`. */
    std::optional<uint256> domainId = std::nullopt;  // NOLINT(readability-redundant-member-init)

    /** Optional bitmask of flags that may be changed after issuance.  When
     *  present must be non-zero and contain only bits within
     *  `tmfMPTokenIssuanceCreateMutableMask`.  Requires `featureDynamicMPT`. */
    std::optional<std::uint32_t> mutableFlags =
        std::nullopt;  // NOLINT(readability-redundant-member-init)
};

/** Transactor for the `ttMPTOKEN_ISSUANCE_CREATE` transaction type.
 *
 *  Creates a new `MPTokenIssuance` ledger object that anchors all subsequent
 *  holder balances for a Multi-Party Token (MPT).  MPTs carry richer metadata
 *  than classic trust-line IOUs: optional supply caps, decimal scaling,
 *  configurable transfer fees, arbitrary binary metadata, and
 *  permissioned-domain constraints.
 *
 *  The core creation logic lives in the static `create()` method rather than
 *  directly in `doApply()`, allowing `VaultCreate` (and future transactors)
 *  to mint an MPT issuance as a side effect by constructing `MPTCreateArgs`
 *  directly, without a full transaction context.
 *
 *  @see MPTCreateArgs
 */
class MPTokenIssuanceCreate : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit MPTokenIssuanceCreate(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Gate optional field categories on the amendments that enable them.
     *
     *  Returns `false` (causing `invokePreflight` to return `temDISABLED`) if:
     *  - `sfDomainID` is present but neither `featurePermissionedDomains` nor
     *    `featureSingleAssetVault` is enabled, or
     *  - `sfMutableFlags` is present but `featureDynamicMPT` is not enabled.
     *
     *  @param ctx  The preflight context.
     *  @return `true` if all present optional fields are covered by active
     *      amendments; `false` otherwise.
     */
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    /** Returns the flag mask for this transaction type.
     *
     *  `invokePreflight` passes this mask to `preflight1`, which rejects any
     *  transaction whose `sfFlags` field contains bits outside the mask.
     *
     *  @param ctx  The preflight context (unused; present for interface
     *      uniformity).
     *  @return `tfMPTokenIssuanceCreateMask`.
     */
    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    /** Validate semantic constraints on the transaction fields.
     *
     *  Enforces four rules:
     *  1. If `sfMutableFlags` is present it must be non-zero and must not
     *     contain bits outside `tmfMPTokenIssuanceCreateMutableMask`
     *     (returns `temINVALID_FLAG`).
     *  2. A non-zero `sfTransferFee` (max 50,000 basis points) requires
     *     `tfMPTCanTransfer` to also be set (returns `temMALFORMED` or
     *     `temBAD_TRANSFER_FEE`).
     *  3. A zero `sfDomainID` is malformed; a non-zero domain ID requires
     *     `tfMPTRequireAuth` (returns `temMALFORMED`).
     *  4. `sfMPTokenMetadata` must be non-empty and ≤ 1,024 bytes;
     *     `sfMaximumAmount`, if present, must be positive and ≤
     *     `maxMPTokenAmount` (returns `temMALFORMED`).
     *
     *  @param ctx  The preflight context.
     *  @return `tesSUCCESS` if all checks pass; a `tem*` error code otherwise.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Apply the transaction: package fields into `MPTCreateArgs` and delegate
     *  to `create()`.
     *
     *  Passes `preFeeBalance_` as `priorBalance` so that `create()` enforces
     *  the reserve requirement against the pre-fee snapshot.
     *
     *  @return `tesSUCCESS` on success, or the `TER` embedded in the
     *      `Unexpected` result from `create()`.
     */
    TER
    doApply() override;

    /** Accumulate per-entry data for transaction-specific invariant checks.
     *
     *  Currently a no-op stub; no transaction-specific invariants are defined
     *  yet.  The method satisfies the `Transactor` interface and serves as the
     *  extension point for future checks.
     *
     *  @param isDelete True if the entry was erased.
     *  @param before   Entry state before the transaction (nullptr for new entries).
     *  @param after    Entry state after the transaction.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Finalize transaction-specific invariant checks.
     *
     *  Currently a no-op stub that always returns `true`.  Satisfies the
     *  `Transactor` interface; reserved for future per-transaction invariants.
     *
     *  @param tx     The transaction being applied.
     *  @param result The tentative TER result.
     *  @param fee    The fee consumed.
     *  @param view   Read-only ledger view after the transaction.
     *  @param j      Logging sink.
     *  @return Always `true` (no invariants to fail).
     */
    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;

    /** Create an `MPTokenIssuance` SLE from the supplied parameters.
     *
     *  This is the single authoritative path for inserting a new MPT issuance
     *  into the ledger.  It is called directly by `doApply()` and may also be
     *  called by other transactors (e.g. `VaultCreate`) that need to mint a
     *  share-token issuance as a side effect.
     *
     *  Sequence of operations:
     *  1. If `args.priorBalance` is set, verifies the account can cover the
     *     new owner-reserve increment.
     *  2. Inserts an entry in the issuer's owner directory via
     *     `view.dirInsert()`.
     *  3. Constructs the `MPTokenIssuance` SLE with mandatory fields
     *     (`sfFlags`, `sfIssuer`, `sfOutstandingAmount = 0`, `sfOwnerNode`,
     *     `sfSequence`) and populates all optional fields present in `args`.
     *  4. Calls `adjustOwnerCount()` to increment the account's reserve
     *     obligation.
     *
     *  @param view    Mutable ledger view to write the new SLE into.
     *  @param journal Logging sink.
     *  @param args    Creation parameters; see `MPTCreateArgs`.
     *  @return On success, the new `MPTID` (deterministically derived from
     *      `args.sequence` and `args.account` via `makeMptID()`).  On failure,
     *      an `Unexpected` wrapping one of:
     *      - `tecINTERNAL` — issuer account SLE not found (ledger corruption).
     *      - `tecINSUFFICIENT_RESERVE` — `priorBalance` too low.
     *      - `tecDIR_FULL` — owner directory page exhausted.
     */
    static Expected<MPTID, TER>
    create(ApplyView& view, beast::Journal journal, MPTCreateArgs const& args);
};

}  // namespace xrpl
