#include <xrpl/tx/transactors/token/MPTokenIssuanceSet.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/DelegateHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Permissions.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <unordered_set>

namespace xrpl {

bool
MPTokenIssuanceSet::checkExtraFeatures(PreflightContext const& ctx)
{
    return !ctx.tx.isFieldPresent(sfDomainID) ||
        (ctx.rules.enabled(featurePermissionedDomains) &&
         ctx.rules.enabled(featureSingleAssetVault));
}

std::uint32_t
MPTokenIssuanceSet::getFlagsMask(PreflightContext const& ctx)
{
    return tfMPTokenIssuanceSetMask;
}

/** Mapping between the set/clear bits in a transaction's `sfMutableFlags`
 *  field and the corresponding `lsmfMPTCanMutate*` gate stored on the
 *  `MPTokenIssuance` SLE.
 *
 *  The three fields implement a two-level flag design: `setFlag` and
 *  `clearFlag` are the transaction-level request bits; `canMutateFlag` is
 *  the issuance-level permission bit that must already be set before the
 *  corresponding change is allowed.  The same struct drives three phases:
 *  `preflight` (conflict detection), `preclaim` (permission gating), and
 *  `doApply` (flag mutation) — ensuring all three always reason about the
 *  same six properties with no possibility of a phase mismatch.
 */
struct MPTMutabilityFlags
{
    std::uint32_t setFlag;        /**< Transaction bit requesting the feature be enabled. */
    std::uint32_t clearFlag;      /**< Transaction bit requesting the feature be disabled. */
    std::uint32_t canMutateFlag;  /**< Issuance SLE bit that gates whether this change is permitted. */
};

/** Table-driven mapping of the six mutable properties of an MPTokenIssuance.
 *
 *  Each entry covers one logical capability (lock, require-auth, escrow,
 *  trade, transfer, clawback).  Using a compile-time array rather than
 *  ad-hoc flag checks ensures that `preflight`, `preclaim`, and `doApply`
 *  always agree on which properties exist and how they map to ledger bits.
 */
static constexpr std::array<MPTMutabilityFlags, 6> kMPT_MUTABILITY_FLAGS = {
    {{.setFlag = tmfMPTSetCanLock,
      .clearFlag = tmfMPTClearCanLock,
      .canMutateFlag = lsmfMPTCanMutateCanLock},
     {.setFlag = tmfMPTSetRequireAuth,
      .clearFlag = tmfMPTClearRequireAuth,
      .canMutateFlag = lsmfMPTCanMutateRequireAuth},
     {.setFlag = tmfMPTSetCanEscrow,
      .clearFlag = tmfMPTClearCanEscrow,
      .canMutateFlag = lsmfMPTCanMutateCanEscrow},
     {.setFlag = tmfMPTSetCanTrade,
      .clearFlag = tmfMPTClearCanTrade,
      .canMutateFlag = lsmfMPTCanMutateCanTrade},
     {.setFlag = tmfMPTSetCanTransfer,
      .clearFlag = tmfMPTClearCanTransfer,
      .canMutateFlag = lsmfMPTCanMutateCanTransfer},
     {.setFlag = tmfMPTSetCanClawback,
      .clearFlag = tmfMPTClearCanClawback,
      .canMutateFlag = lsmfMPTCanMutateCanClawback}}};

/** Stateless validation for `MPTokenIssuanceSet` transactions.
 *
 *  Operates in two modes depending on which fields are present:
 *
 *  **Lock/unlock mode** (no `sfMutableFlags`, `sfMPTokenMetadata`, or
 *  `sfTransferFee`): validates flag exclusivity (`tfMPTLock` and
 *  `tfMPTUnlock` cannot both be set) and that the submitter is not also
 *  the named holder.  The no-op check under `featureSingleAssetVault` or
 *  `featureDynamicMPT` rejects a transaction that changes nothing at all
 *  (zero flags, no domain, no mutation fields), preventing fee-burning
 *  submissions.
 *
 *  **Mutation mode** (any of `sfMutableFlags`, `sfMPTokenMetadata`,
 *  `sfTransferFee` present): requires `featureDynamicMPT`; `sfHolder` and
 *  non-universal tx flags are both forbidden because mutation targets the
 *  issuance object rather than any individual holder slot.  The check for a
 *  non-zero `sfTransferFee` alongside `tmfMPTClearCanTransfer` in
 *  `sfMutableFlags` catches a single-transaction contradiction (setting a
 *  fee while simultaneously removing transfer capability) before it reaches
 *  the ledger.
 *
 *  The mutual exclusion of `sfDomainID` and `sfHolder` is validated here
 *  because domain assignment operates on the issuance object while holder
 *  operations target an individual `MPToken` slot — the two cannot coexist.
 */
NotTEC
MPTokenIssuanceSet::preflight(PreflightContext const& ctx)
{
    auto const mutableFlags = ctx.tx[~sfMutableFlags];
    auto const metadata = ctx.tx[~sfMPTokenMetadata];
    auto const transferFee = ctx.tx[~sfTransferFee];
    auto const isMutate = mutableFlags || metadata || transferFee;

    if (isMutate && !ctx.rules.enabled(featureDynamicMPT))
        return temDISABLED;

    if (ctx.tx.isFieldPresent(sfDomainID) && ctx.tx.isFieldPresent(sfHolder))
        return temMALFORMED;

    auto const txFlags = ctx.tx.getFlags();

    if (((txFlags & tfMPTLock) != 0u) && ((txFlags & tfMPTUnlock) != 0u))
        return temINVALID_FLAG;

    auto const accountID = ctx.tx[sfAccount];
    auto const holderID = ctx.tx[~sfHolder];
    if (holderID && accountID == holderID)
        return temMALFORMED;

    if (ctx.rules.enabled(featureSingleAssetVault) || ctx.rules.enabled(featureDynamicMPT))
    {
        if (txFlags == 0 && !ctx.tx.isFieldPresent(sfDomainID) && !isMutate)
            return temMALFORMED;
    }

    if (ctx.rules.enabled(featureDynamicMPT))
    {
        if (isMutate && holderID)
            return temMALFORMED;

        if (isMutate && ((txFlags & tfUniversalMask) != 0u))
            return temMALFORMED;

        if (transferFee && *transferFee > kMAX_TRANSFER_FEE)
            return temBAD_TRANSFER_FEE;

        if (metadata && metadata->length() > kMAX_MP_TOKEN_METADATA_LENGTH)
            return temMALFORMED;

        if (mutableFlags)
        {
            if ((*mutableFlags == 0u) || ((*mutableFlags & tmfMPTokenIssuanceSetMutableMask) != 0u))
                return temINVALID_FLAG;

            if (std::ranges::any_of(kMPT_MUTABILITY_FLAGS, [mutableFlags](auto const& f) {
                    return (*mutableFlags & f.setFlag) && (*mutableFlags & f.clearFlag);
                }))
                return temINVALID_FLAG;

            if ((transferFee.value_or(0) != 0u) && ((*mutableFlags & tmfMPTClearCanTransfer) != 0u))
                return temMALFORMED;
        }
    }

    return tesSUCCESS;
}

/** Two-tier delegate authorization check for lock/unlock operations.
 *
 *  If `sfDelegate` is absent the issuer signed directly and is
 *  unconditionally permitted (early return `tesSUCCESS`).
 *
 *  When a delegate is present, authorization is evaluated in order:
 *  1. A broad transaction-type permission via `checkTxPermission()` — if
 *     the delegate holds blanket `ttMPTOKEN_ISSUANCE_SET` authority, no
 *     further checks are needed.
 *  2. A flag-level guard is applied first (currently unreachable because no
 *     transaction-level flags beyond lock/unlock exist, but retained as
 *     forward-compatibility infrastructure — see `LCOV_EXCL_LINE`).
 *  3. Granular fall-back: `MPTokenIssuanceLock` is required for `tfMPTLock`
 *     and `MPTokenIssuanceUnlock` is required for `tfMPTUnlock`.  The
 *     `loadGranularPermission()` helper populates the set from the delegate
 *     SLE's permission list.
 */
NotTEC
MPTokenIssuanceSet::checkPermission(ReadView const& view, STTx const& tx)
{
    auto const delegate = tx[~sfDelegate];
    if (!delegate)
        return tesSUCCESS;

    auto const delegateKey = keylet::delegate(tx[sfAccount], *delegate);
    auto const sle = view.read(delegateKey);

    if (!sle)
        return terNO_DELEGATE_PERMISSION;

    if (isTesSuccess(checkTxPermission(sle, tx)))
        return tesSUCCESS;

    auto const txFlags = tx.getFlags();

    // this is added in case more flags will be added for MPTokenIssuanceSet
    // in the future. Currently unreachable.
    if ((txFlags & tfMPTokenIssuanceSetMask) != 0u)
        return terNO_DELEGATE_PERMISSION;  // LCOV_EXCL_LINE

    std::unordered_set<GranularPermissionType> granularPermissions;
    loadGranularPermission(sle, ttMPTOKEN_ISSUANCE_SET, granularPermissions);

    if (((txFlags & tfMPTLock) != 0u) && !granularPermissions.contains(MPTokenIssuanceLock))
        return terNO_DELEGATE_PERMISSION;

    if (((txFlags & tfMPTUnlock) != 0u) && !granularPermissions.contains(MPTokenIssuanceUnlock))
        return terNO_DELEGATE_PERMISSION;

    return tesSUCCESS;
}

/** Read-only ledger-state validation for `MPTokenIssuanceSet`.
 *
 *  Checks are ordered from cheap/general to expensive/specific:
 *
 *  1. **Issuance existence and ownership**: the `MPTokenIssuance` SLE must
 *     exist and its `sfIssuer` must match the submitting account.
 *
 *  2. **`lsfMPTCanLock` gate** (asymmetric): under the original rules the
 *     flag must be set for _any_ transaction.  Under `featureSingleAssetVault`
 *     or `featureDynamicMPT`, the flag is only required when the transaction
 *     actually requests a lock or unlock — mutation-only transactions on
 *     issuances that lack locking capability are still permitted.  Two
 *     separate `if` blocks are used rather than `||` to preserve this
 *     readable asymmetric logic.
 *
 *  3. **Holder checks** (when `sfHolder` present): the holder account must
 *     exist and the holder's `MPToken` slot for this issuance must exist.
 *
 *  4. **Domain checks** (when `sfDomainID` present): the issuance must have
 *     `lsfMPTRequireAuth` set (binding a domain to an unauthorized issuance
 *     is meaningless); a non-zero domain ID must reference an existing
 *     `PermissionedDomain` object.
 *
 *  5. **Mutation permission checks** (`featureDynamicMPT`): each flag the
 *     transaction sets or clears via `sfMutableFlags` must have the
 *     corresponding `lsmfMPTCanMutate*` bit already set on the issuance SLE
 *     (table-driven via `kMPT_MUTABILITY_FLAGS`).  Clearing
 *     `lsfMPTRequireAuth` while a `DomainID` is set on the issuance is
 *     blocked here because it would produce an internally inconsistent
 *     ledger state.  `sfMPTokenMetadata` and `sfTransferFee` mutations
 *     require `lsmfMPTCanMutateMetadata` and `lsmfMPTCanMutateTransferFee`
 *     respectively.
 *
 *  6. **Transfer fee pre-existence requirement**: a non-zero `sfTransferFee`
 *     requires `lsfMPTCanTransfer` to already be set on the _current_ ledger
 *     object.  Enabling `tmfMPTSetCanTransfer` in the same transaction does
 *     not satisfy this requirement — `preclaim` is the first phase where the
 *     current ledger value of the flag is visible, so this check belongs here
 *     rather than in `preflight`.
 */
TER
MPTokenIssuanceSet::preclaim(PreclaimContext const& ctx)
{
    auto const sleMptIssuance = ctx.view.read(keylet::mptIssuance(ctx.tx[sfMPTokenIssuanceID]));
    if (!sleMptIssuance)
        return tecOBJECT_NOT_FOUND;

    if (!sleMptIssuance->isFlag(lsfMPTCanLock))
    {
        if (!ctx.view.rules().enabled(featureSingleAssetVault) &&
            !ctx.view.rules().enabled(featureDynamicMPT))
        {
            return tecNO_PERMISSION;
        }
        if (ctx.tx.isFlag(tfMPTLock) || ctx.tx.isFlag(tfMPTUnlock))
        {
            return tecNO_PERMISSION;
        }
    }

    if ((*sleMptIssuance)[sfIssuer] != ctx.tx[sfAccount])
        return tecNO_PERMISSION;

    if (auto const holderID = ctx.tx[~sfHolder])
    {
        if (!ctx.view.exists(keylet::account(*holderID)))
            return tecNO_DST;

        if (!ctx.view.exists(keylet::mptoken(ctx.tx[sfMPTokenIssuanceID], *holderID)))
            return tecOBJECT_NOT_FOUND;
    }

    if (auto const domain = ctx.tx[~sfDomainID])
    {
        if (not sleMptIssuance->isFlag(lsfMPTRequireAuth))
            return tecNO_PERMISSION;

        if (*domain != beast::kZERO)
        {
            auto const sleDomain = ctx.view.read(keylet::permissionedDomain(*domain));
            if (!sleDomain)
                return tecOBJECT_NOT_FOUND;
        }
    }

    // sfMutableFlags defaults to 0 when absent (soeDEFAULT field).
    auto const currentMutableFlags = sleMptIssuance->getFieldU32(sfMutableFlags);

    auto isMutableFlag = [&](std::uint32_t mutableFlag) -> bool {
        return currentMutableFlags & mutableFlag;
    };

    if (auto const mutableFlags = ctx.tx[~sfMutableFlags])
    {
        if (std::ranges::any_of(
                kMPT_MUTABILITY_FLAGS, [mutableFlags, &isMutableFlag](auto const& f) {
                    return !isMutableFlag(f.canMutateFlag) &&
                        ((*mutableFlags & (f.setFlag | f.clearFlag)));
                }))
            return tecNO_PERMISSION;

        // Clearing lsfMPTRequireAuth is invalid when the issuance already has
        // a DomainID set, because a DomainID requires RequireAuth to be active.
        if ((*mutableFlags & tmfMPTClearRequireAuth) != 0u &&
            sleMptIssuance->isFieldPresent(sfDomainID))
            return tecNO_PERMISSION;
    }

    if (!isMutableFlag(lsmfMPTCanMutateMetadata) && ctx.tx.isFieldPresent(sfMPTokenMetadata))
        return tecNO_PERMISSION;

    if (auto const fee = ctx.tx[~sfTransferFee])
    {
        if (fee > 0u && !sleMptIssuance->isFlag(lsfMPTCanTransfer))
            return tecNO_PERMISSION;

        if (!isMutableFlag(lsmfMPTCanMutateTransferFee))
            return tecNO_PERMISSION;
    }

    return tesSUCCESS;
}

/** Apply the mutations described by the transaction to the ledger.
 *
 *  **SLE selection**: if `sfHolder` is present the holder's `MPToken` SLE is
 *  used; otherwise the `MPTokenIssuance` SLE is used.  This single branch
 *  lets both issuance-wide and per-holder locking share the same lock/unlock
 *  code path.  A missing SLE at this point is a ledger-corruption sentinel
 *  (`tecINTERNAL`) since `preclaim` already verified existence.
 *
 *  **Lock/unlock**: `lsfMPTLocked` is set or cleared directly on whichever
 *  SLE was selected.  The two flags are mutually exclusive (enforced in
 *  `preflight`), so a simple if/else-if suffices.
 *
 *  **Mutable-flag iteration**: `kMPT_MUTABILITY_FLAGS` is iterated to
 *  translate each `tmfMPTSet*`/`tmfMPTClear*` bit in `sfMutableFlags` into
 *  the corresponding `lsmfMPTCanMutate*` bit change on the issuance `sfFlags`.
 *  Clearing `tmfMPTClearCanTransfer` atomically removes `sfTransferFee` from
 *  the same SLE — you cannot have a fee-bearing issuance with transfer
 *  capability disabled, and removing the fee in the same write keeps the
 *  ledger internally consistent.
 *
 *  **`soeDEFAULT` semantics for `sfTransferFee`**: a zero fee value removes
 *  the field entirely rather than storing zero, matching the convention that
 *  an absent field is interpreted as zero by readers.
 *
 *  **`soeDEFAULT` semantics for `sfMPTokenMetadata`**: an empty blob removes
 *  the field entirely.
 *
 *  **`beast::kZERO` sentinel for `sfDomainID`**: the zero 256-bit value
 *  signals "remove the existing domain link"; any other value sets or
 *  replaces the domain.  This mirrors the sentinel-clear pattern used for
 *  similar optional-reference fields elsewhere in the protocol.
 *
 *  All changes are written in a single `view().update(sle)` at the end.
 */
TER
MPTokenIssuanceSet::doApply()
{
    auto const mptIssuanceID = ctx_.tx[sfMPTokenIssuanceID];
    auto const txFlags = ctx_.tx.getFlags();
    auto const holderID = ctx_.tx[~sfHolder];
    auto const domainID = ctx_.tx[~sfDomainID];
    std::shared_ptr<SLE> sle;

    if (holderID)
    {
        sle = view().peek(keylet::mptoken(mptIssuanceID, *holderID));
    }
    else
    {
        sle = view().peek(keylet::mptIssuance(mptIssuanceID));
    }

    if (!sle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    std::uint32_t const flagsIn = sle->getFieldU32(sfFlags);
    std::uint32_t flagsOut = flagsIn;

    if ((txFlags & tfMPTLock) != 0u)
    {
        flagsOut |= lsfMPTLocked;
    }
    else if ((txFlags & tfMPTUnlock) != 0u)
    {
        flagsOut &= ~lsfMPTLocked;
    }

    if (auto const mutableFlags = ctx_.tx[~sfMutableFlags].value_or(0))
    {
        for (auto const& f : kMPT_MUTABILITY_FLAGS)
        {
            if ((mutableFlags & f.setFlag) != 0u)
            {
                flagsOut |= f.canMutateFlag;
            }
            else if ((mutableFlags & f.clearFlag) != 0u)
            {
                flagsOut &= ~f.canMutateFlag;
            }
        }

        if ((mutableFlags & tmfMPTClearCanTransfer) != 0u)
            sle->makeFieldAbsent(sfTransferFee);
    }

    if (flagsIn != flagsOut)
        sle->setFieldU32(sfFlags, flagsOut);

    if (auto const transferFee = ctx_.tx[~sfTransferFee])
    {
        if (transferFee == 0)
        {
            sle->makeFieldAbsent(sfTransferFee);
        }
        else
        {
            sle->setFieldU16(sfTransferFee, *transferFee);
        }
    }

    if (auto const metadata = ctx_.tx[~sfMPTokenMetadata])
    {
        if (metadata->empty())
        {
            sle->makeFieldAbsent(sfMPTokenMetadata);
        }
        else
        {
            sle->setFieldVL(sfMPTokenMetadata, *metadata);
        }
    }

    if (domainID)
    {
        XRPL_ASSERT(
            sle->getType() == ltMPTOKEN_ISSUANCE,
            "MPTokenIssuanceSet::doApply : modifying MPTokenIssuance");

        if (*domainID != beast::kZERO)
        {
            sle->setFieldH256(sfDomainID, *domainID);
        }
        else
        {
            if (sle->isFieldPresent(sfDomainID))
                sle->makeFieldAbsent(sfDomainID);
        }
    }

    view().update(sle);

    return tesSUCCESS;
}

void
MPTokenIssuanceSet::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
MPTokenIssuanceSet::finalizeInvariants(
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
