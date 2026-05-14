#include <xrpl/tx/transactors/token/MPTokenIssuanceCreate.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <memory>

namespace xrpl {

/** Gate optional field categories on the amendments that enable them.
 *
 *  Runs before the main flag-mask check in `invokePreflight`, so rejections
 *  here are cheaper than a full preflight.  Two independent gates are applied:
 *
 *  - `sfDomainID` is only meaningful when both `featurePermissionedDomains`
 *    and `featureSingleAssetVault` are active.  Requiring both ensures the
 *    domain registry and the vault subsystem are available before any
 *    domain-scoped issuance can be created.
 *
 *  - `sfMutableFlags` requires `featureDynamicMPT`.  Without that amendment
 *    the ledger has no mechanism to honor mutability requests, so a non-null
 *    value would be silently dropped — returning false here prevents that.
 *
 *  @return `true` if all present gated fields are covered by active amendments;
 *      `false` causes `invokePreflight` to return `temDISABLED`.
 */
bool
MPTokenIssuanceCreate::checkExtraFeatures(PreflightContext const& ctx)
{
    if (ctx.tx.isFieldPresent(sfDomainID) &&
        !(ctx.rules.enabled(featurePermissionedDomains) &&
          ctx.rules.enabled(featureSingleAssetVault)))
        return false;

    if (ctx.tx.isFieldPresent(sfMutableFlags) && !ctx.rules.enabled(featureDynamicMPT))
        return false;

    return true;
}

/** Return the valid-flags mask for this transaction type.
 *
 *  The framework passes this value to `preflight1`, which rejects any
 *  transaction whose `sfFlags` field has bits set outside the mask.  The
 *  mask constant `tfMPTokenIssuanceCreateMask` is defined in `TxFlags.h` and
 *  covers all flags meaningful to MPT issuance creation (e.g.
 *  `tfMPTCanTransfer`, `tfMPTRequireAuth`, `tfMPTCanEscrow`, etc.).
 *
 *  The `ctx` parameter is accepted for interface uniformity but unused; the
 *  mask is unconditional for this transaction type.
 */
std::uint32_t
MPTokenIssuanceCreate::getFlagsMask(PreflightContext const& ctx)
{
    return tfMPTokenIssuanceCreateMask;
}

/** Validate the semantic field-level constraints of an MPT issuance creation.
 *
 *  This runs after `checkExtraFeatures` and `preflight1` have passed, so
 *  amendment gates and unknown flag bits are already ruled out.  The checks
 *  are ordered from cheapest to most likely to fail in practice:
 *
 *  1. **`sfMutableFlags`**: If present, must be non-zero (at least one mutable
 *     flag declared) and must not have bits set outside
 *     `tmfMPTokenIssuanceCreateMutableMask`.  The condition
 *     `(*mutableFlags & mask) != 0` is true when reserved bits ARE set — a
 *     defensive pattern in this codebase where the mask captures allowed bits,
 *     not disallowed ones.  Returns `temINVALID_FLAG`.
 *
 *  2. **`sfTransferFee`**: Must not exceed `kMAX_TRANSFER_FEE` (50,000 basis
 *     points = 50%).  A non-zero fee without `tfMPTCanTransfer` is incoherent
 *     by protocol design — the fee would never be applied — so that
 *     combination returns `temMALFORMED`.
 *
 *  3. **`sfDomainID`**: An all-zeros value (`beast::kZERO`) is rejected as a
 *     sentinel for "no domain".  A valid domain ID mandates `tfMPTRequireAuth`
 *     because a domain-scoped issuance restricts which holders are eligible;
 *     without authorization the domain constraint cannot be enforced.  Returns
 *     `temMALFORMED` for either violation.
 *
 *  4. **`sfMPTokenMetadata`**: Must be non-empty and at most
 *     `kMAX_MP_TOKEN_METADATA_LENGTH` (1,024) bytes.  Returns `temMALFORMED`.
 *
 *  5. **`sfMaximumAmount`**: Must be positive and ≤ `kMAX_MP_TOKEN_AMOUNT`
 *     (0x7FFF_FFFF_FFFF_FFFF).  The ceiling ensures amounts fit within the
 *     XRPL `Number` type's representable range.  Returns `temMALFORMED`.
 *
 *  @return `tesSUCCESS` if all checks pass; a `tem*` error code otherwise.
 */
NotTEC
MPTokenIssuanceCreate::preflight(PreflightContext const& ctx)
{
    if (auto const mutableFlags = ctx.tx[~sfMutableFlags]; mutableFlags &&
        ((*mutableFlags == 0u) || ((*mutableFlags & tmfMPTokenIssuanceCreateMutableMask) != 0u)))
        return temINVALID_FLAG;

    if (auto const fee = ctx.tx[~sfTransferFee])
    {
        if (fee > kMAX_TRANSFER_FEE)
            return temBAD_TRANSFER_FEE;

        // If a non-zero TransferFee is set then the tfTransferable flag
        // must also be set.
        if (fee > 0u && !ctx.tx.isFlag(tfMPTCanTransfer))
            return temMALFORMED;
    }

    if (auto const domain = ctx.tx[~sfDomainID])
    {
        if (*domain == beast::kZERO)
            return temMALFORMED;

        // Domain present implies that MPTokenIssuance is not public
        if ((ctx.tx.getFlags() & tfMPTRequireAuth) == 0)
            return temMALFORMED;
    }

    if (auto const metadata = ctx.tx[~sfMPTokenMetadata])
    {
        if (metadata->empty() || metadata->length() > kMAX_MP_TOKEN_METADATA_LENGTH)
            return temMALFORMED;
    }

    if (auto const maxAmt = ctx.tx[~sfMaximumAmount])
    {
        if (maxAmt == 0)
            return temMALFORMED;

        if (maxAmt > kMAX_MP_TOKEN_AMOUNT)
            return temMALFORMED;
    }
    return tesSUCCESS;
}

/** Create a new `MPTokenIssuance` SLE and insert it into the ledger.
 *
 *  This is the single authoritative path for all MPT issuance creation.
 *  `doApply()` calls it directly; `VaultCreate` calls it to mint the
 *  vault's share token from a pseudo-account.  Keeping the logic here rather
 *  than inline in `doApply()` allows other transactors to reuse it without a
 *  full `ApplyContext`.
 *
 *  **Execution sequence:**
 *
 *  1. Peek the issuer's `AccountRoot` SLE (write access required for the
 *     subsequent `adjustOwnerCount`).  A missing account returns
 *     `tecINTERNAL`; this branch is excluded from coverage because a valid
 *     transaction reaching apply phase always has its account in the ledger.
 *
 *  2. If `args.priorBalance` is provided, verify the issuer can cover the
 *     reserve for `ownerCount + 1` new objects.  Callers that manage the
 *     reserve externally — most notably `VaultCreate`, which operates on a
 *     freshly-created pseudo-account — pass `std::nullopt` to skip this gate.
 *
 *  3. Compute the deterministic `MPTID` via `makeMptID(args.sequence,
 *     args.account)`.  The 192-bit identifier is derived from the issuer's
 *     `AccountID` and the transaction sequence number; monotonically
 *     increasing sequences guarantee uniqueness under normal ledger operation.
 *
 *  4. Insert the issuance into the issuer's owner directory via
 *     `view.dirInsert()`.  A full directory returns `tecDIR_FULL`; also
 *     excluded from coverage as a theoretical-only edge case.
 *
 *  5. Construct the `MPTokenIssuance` SLE.  Mandatory fields written
 *     unconditionally: `sfFlags` (with universal bits stripped via
 *     `~tfUniversal`), `sfIssuer`, `sfOutstandingAmount` (initialized to 0),
 *     `sfOwnerNode`, and `sfSequence`.  All optional fields
 *     (`sfMaximumAmount`, `sfAssetScale`, `sfTransferFee`,
 *     `sfMPTokenMetadata`, `sfDomainID`, `sfMutableFlags`) are written only
 *     when present in `args`, keeping the SLE sparse.  `sfOutstandingAmount`
 *     starting at 0 is the exact condition that `MPTokenIssuanceDestroy`
 *     checks — the ledger enforces that an issuance with circulating supply
 *     cannot be destroyed.
 *
 *  6. Increment `sfOwnerCount` via `adjustOwnerCount(+1)`.  This raises the
 *     issuer's reserve threshold and protects the issuance from
 *     garbage-collection until it is explicitly destroyed.
 *
 *  @param view    Mutable ledger view to write into.
 *  @param journal Logging sink.
 *  @param args    Aggregate of all creation parameters; see `MPTCreateArgs`.
 *  @return On success, the newly minted `MPTID`.  On failure, an `Unexpected`
 *      wrapping `tecINTERNAL`, `tecINSUFFICIENT_RESERVE`, or `tecDIR_FULL`.
 */
Expected<MPTID, TER>
MPTokenIssuanceCreate::create(ApplyView& view, beast::Journal journal, MPTCreateArgs const& args)
{
    auto const acct = view.peek(keylet::account(args.account));
    if (!acct)
        return Unexpected(tecINTERNAL);  // LCOV_EXCL_LINE

    if (args.priorBalance &&
        *(args.priorBalance) < view.fees().accountReserve((*acct)[sfOwnerCount] + 1))
        return Unexpected(tecINSUFFICIENT_RESERVE);

    auto const mptId = makeMptID(args.sequence, args.account);
    auto const mptIssuanceKeylet = keylet::mptIssuance(mptId);

    {
        auto const ownerNode = view.dirInsert(
            keylet::ownerDir(args.account), mptIssuanceKeylet, describeOwnerDir(args.account));

        if (!ownerNode)
            return Unexpected(tecDIR_FULL);  // LCOV_EXCL_LINE

        auto mptIssuance = std::make_shared<SLE>(mptIssuanceKeylet);
        (*mptIssuance)[sfFlags] = args.flags & ~tfUniversal;
        (*mptIssuance)[sfIssuer] = args.account;
        (*mptIssuance)[sfOutstandingAmount] = 0;
        (*mptIssuance)[sfOwnerNode] = *ownerNode;
        (*mptIssuance)[sfSequence] = args.sequence;

        if (args.maxAmount)
            (*mptIssuance)[sfMaximumAmount] = *args.maxAmount;

        if (args.assetScale)
            (*mptIssuance)[sfAssetScale] = *args.assetScale;

        if (args.transferFee)
            (*mptIssuance)[sfTransferFee] = *args.transferFee;

        if (args.metadata)
            (*mptIssuance)[sfMPTokenMetadata] = *args.metadata;

        if (args.domainId)
            (*mptIssuance)[sfDomainID] = *args.domainId;

        if (args.mutableFlags)
            (*mptIssuance)[sfMutableFlags] = *args.mutableFlags;

        view.insert(mptIssuance);
    }

    adjustOwnerCount(view, acct, 1, journal);

    return mptId;
}

/** Apply the transaction by packaging its fields into `MPTCreateArgs` and
 *  delegating all ledger mutation to `create()`.
 *
 *  `preFeeBalance_` is passed as `priorBalance` so the reserve check inside
 *  `create()` uses the pre-fee snapshot — consistent with the codebase
 *  convention that reserve adequacy is measured before the fee is deducted
 *  (see "Reserve Check Convention" in the transactors skill).
 *
 *  All optional transaction fields are forwarded unchanged; absent fields
 *  become `std::nullopt` in `MPTCreateArgs` and are omitted from the SLE,
 *  keeping the ledger entry sparse.
 *
 *  @return `tesSUCCESS` if `create()` succeeds; otherwise the `TER` embedded
 *      in the `Unexpected` result (one of `tecINTERNAL`,
 *      `tecINSUFFICIENT_RESERVE`, or `tecDIR_FULL`).
 */
TER
MPTokenIssuanceCreate::doApply()
{
    auto const& tx = ctx_.tx;
    auto const result = create(
        view(),
        j_,
        {
            .priorBalance = preFeeBalance_,
            .account = account_,
            .sequence = tx.getSeqValue(),
            .flags = tx.getFlags(),
            .maxAmount = tx[~sfMaximumAmount],
            .assetScale = tx[~sfAssetScale],
            .transferFee = tx[~sfTransferFee],
            .metadata = tx[~sfMPTokenMetadata],
            .domainId = tx[~sfDomainID],
            .mutableFlags = tx[~sfMutableFlags],
        });
    return result ? tesSUCCESS : result.error();
}

/** No-op stub satisfying the `Transactor` invariant-entry interface.
 *
 *  No transaction-specific invariants are defined for
 *  `MPTokenIssuanceCreate` yet.  The global `ValidMPTIssuance` and
 *  `ValidMPTPayment` checkers in `InvariantCheck.cpp` cover the structural
 *  properties of the newly inserted SLE.  This method exists as an extension
 *  point for future per-transaction checks (e.g., verifying that exactly one
 *  new `ltMPTOKEN_ISSUANCE` entry appears in the diff).
 */
void
MPTokenIssuanceCreate::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

/** No-op stub satisfying the `Transactor` invariant-finalize interface.
 *
 *  Always returns `true`.  No transaction-specific invariants are defined for
 *  `MPTokenIssuanceCreate` yet; the global invariant framework handles all
 *  structural checks.  Reserved as an extension point for future checks.
 *
 *  @return `true` unconditionally.
 */
bool
MPTokenIssuanceCreate::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
