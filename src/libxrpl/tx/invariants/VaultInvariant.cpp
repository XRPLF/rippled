#include <xrpl/tx/invariants/VaultInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/invariants/InvariantCheckPrivilege.h>
#include <xrpl/tx/transactors/vault/VaultInvariantData.h>

#include <algorithm>
#include <memory>
#include <optional>

namespace xrpl {

static constexpr Number kZero{};

using Vault = VaultInvariantData::Vault;
using Shares = VaultInvariantData::Shares;
using DeltaInfo = VaultInvariantData::DeltaInfo;

void
ValidVault::visitEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after)
{
    data_.visitEntry(isDelete, before, after);
}

bool
ValidVault::finalize(
    STTx const& tx,
    TER const ret,
    XRPAmount const fee,
    ReadView const& view,
    beast::Journal const& j)
{
    bool const enforce = view.rules().enabled(featureSingleAssetVault);

    if (!isTesSuccess(ret))
        return true;  // Do not perform checks

    if (data_.afterVault().empty() && data_.beforeVault().empty())
    {
        if (hasPrivilege(tx, MustModifyVault))
        {
            JLOG(j.fatal()) <<  //
                "Invariant failed: vault operation succeeded without modifying "
                "a vault";
            XRPL_ASSERT(enforce, "xrpl::ValidVault::finalize : vault noop invariant");
            return !enforce;
        }

        return true;  // Not a vault operation
    }
    if (!(hasPrivilege(tx, MustModifyVault) || hasPrivilege(tx, MayModifyVault)))
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: vault updated by a wrong transaction type";
        XRPL_ASSERT(
            enforce,
            "xrpl::ValidVault::finalize : illegal vault transaction "
            "invariant");
        return !enforce;  // Also not a vault operation
    }

    if (data_.beforeVault().size() > 1 || data_.afterVault().size() > 1)
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: vault operation updated more than single vault";
        XRPL_ASSERT(enforce, "xrpl::ValidVault::finalize : single vault invariant");
        return !enforce;  // That's all we can do here
    }

    auto const txnType = tx.getTxnType();

    // We do special handling for ttVAULT_DELETE first, because it's the only
    // vault-modifying transaction without an "after" state of the vault
    if (data_.afterVault().empty())
    {
        if (txnType != ttVAULT_DELETE)
        {
            JLOG(j.fatal()) <<  //
                "Invariant failed: vault deleted by a wrong transaction type";
            XRPL_ASSERT(
                enforce,
                "xrpl::ValidVault::finalize : illegal vault deletion "
                "invariant");
            return !enforce;  // That's all we can do here
        }

        // Note, if afterVault_ is empty then we know that beforeVault_ is not
        // empty, as enforced at the top of this function
        auto const& beforeVault = data_.beforeVault()[0];

        auto const deletedShares = data_.resolveBeforeShares(beforeVault);

        if (!deletedShares)
        {
            JLOG(j.fatal()) << "Invariant failed: deleted vault must also "
                               "delete shares";
            XRPL_ASSERT(enforce, "xrpl::ValidVault::finalize : shares deletion invariant");
            return !enforce;  // That's all we can do here
        }

        bool result = true;
        if (deletedShares->sharesTotal != 0)
        {
            JLOG(j.fatal()) << "Invariant failed: deleted vault must have no "
                               "shares outstanding";
            result = false;
        }
        if (beforeVault.assetsTotal != kZero)
        {
            JLOG(j.fatal()) << "Invariant failed: deleted vault must have no "
                               "assets outstanding";
            result = false;
        }
        if (beforeVault.assetsAvailable != kZero)
        {
            JLOG(j.fatal()) << "Invariant failed: deleted vault must have no "
                               "assets available";
            result = false;
        }

        return result;
    }
    if (txnType == ttVAULT_DELETE)
    {
        JLOG(j.fatal()) << "Invariant failed: vault deletion succeeded without "
                           "deleting a vault";
        XRPL_ASSERT(enforce, "xrpl::ValidVault::finalize : vault deletion invariant");
        return !enforce;  // That's all we can do here
    }

    // Note, `afterVault_.empty()` is handled above
    auto const& afterVault = data_.afterVault()[0];
    XRPL_ASSERT(
        data_.beforeVault().empty() || data_.beforeVault()[0].key == afterVault.key,
        "xrpl::ValidVault::finalize : single vault operation");

    auto const updatedShares = [&]() -> std::optional<Shares> {
        // Check the in-memory collection first (covers the common case where
        // the issuance was touched by this transaction).
        if (auto found = data_.resolveUpdatedShares(afterVault))
            return found;

        // Fall back to reading from the view for transactions that do not
        // modify the issuance object itself (e.g. VaultSet).
        auto const sleShares = view.read(keylet::mptokenIssuance(afterVault.shareMPTID));
        return sleShares ? std::optional<Shares>(Shares::make(*sleShares)) : std::nullopt;
    }();

    bool result = true;

    // Universal transaction checks
    if (!data_.beforeVault().empty())
    {
        auto const& beforeVault = data_.beforeVault()[0];
        if (afterVault.asset != beforeVault.asset || afterVault.pseudoId != beforeVault.pseudoId ||
            afterVault.shareMPTID != beforeVault.shareMPTID)
        {
            JLOG(j.fatal()) << "Invariant failed: violation of vault immutable data";
            result = false;
        }
    }

    if (!updatedShares)
    {
        JLOG(j.fatal()) << "Invariant failed: updated vault must have shares";
        XRPL_ASSERT(enforce, "xrpl::ValidVault::finalize : vault has shares invariant");
        return !enforce;  // That's all we can do here
    }

    if (updatedShares->sharesTotal == 0)
    {
        if (afterVault.assetsTotal != kZero)
        {
            JLOG(j.fatal()) << "Invariant failed: updated zero sized "
                               "vault must have no assets outstanding";
            result = false;
        }
        if (afterVault.assetsAvailable != kZero)
        {
            JLOG(j.fatal()) << "Invariant failed: updated zero sized "
                               "vault must have no assets available";
            result = false;
        }
    }
    else if (updatedShares->sharesTotal > updatedShares->sharesMaximum)
    {
        JLOG(j.fatal())  //
            << "Invariant failed: updated shares must not exceed maximum "
            << updatedShares->sharesMaximum;
        result = false;
    }

    if (afterVault.assetsAvailable < kZero)
    {
        JLOG(j.fatal()) << "Invariant failed: assets available must be positive";
        result = false;
    }

    if (afterVault.assetsAvailable > afterVault.assetsTotal)
    {
        JLOG(j.fatal()) << "Invariant failed: assets available must "
                           "not be greater than assets outstanding";
        result = false;
    }
    else if (afterVault.lossUnrealized > afterVault.assetsTotal - afterVault.assetsAvailable)
    {
        JLOG(j.fatal())  //
            << "Invariant failed: loss unrealized must not exceed "
               "the difference between assets outstanding and available";
        result = false;
    }

    if (afterVault.assetsTotal < kZero)
    {
        JLOG(j.fatal()) << "Invariant failed: assets outstanding must be positive";
        result = false;
    }

    if (afterVault.assetsMaximum < kZero)
    {
        JLOG(j.fatal()) << "Invariant failed: assets maximum must be positive";
        result = false;
    }

    // Thanks to this check we can simply do `assert(!beforeVault_.empty()` when
    // enforcing invariants on transaction types other than ttVAULT_CREATE
    if (data_.beforeVault().empty() && txnType != ttVAULT_CREATE)
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: vault created by a wrong transaction type";
        XRPL_ASSERT(enforce, "xrpl::ValidVault::finalize : vault creation invariant");
        return !enforce;  // That's all we can do here
    }

    if (!data_.beforeVault().empty() &&
        afterVault.lossUnrealized != data_.beforeVault()[0].lossUnrealized &&
        txnType != ttLOAN_MANAGE && txnType != ttLOAN_PAY)
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: vault transaction must not change loss "
            "unrealized";
        result = false;
    }

    auto const beforeShares = [&]() -> std::optional<Shares> {
        if (data_.beforeVault().empty())
            return std::nullopt;
        return data_.resolveBeforeShares(data_.beforeVault()[0]);
    }();

    if (!beforeShares &&
        (tx.getTxnType() == ttVAULT_DEPOSIT ||   //
         tx.getTxnType() == ttVAULT_WITHDRAW ||  //
         tx.getTxnType() == ttVAULT_CLAWBACK))
    {
        JLOG(j.fatal()) << "Invariant failed: vault operation succeeded "
                           "without updating shares";
        XRPL_ASSERT(enforce, "xrpl::ValidVault::finalize : shares noop invariant");
        return !enforce;  // That's all we can do here
    }

    auto const& vaultAsset = afterVault.asset;

    // Technically this does not need to be a lambda, but it's more
    // convenient thanks to early "return false"; the not-so-nice
    // alternatives are several layers of nested if/else or more complex
    // (i.e. brittle) if statements.
    result &= [&]() {
        switch (txnType)
        {
            case ttVAULT_CREATE: {
                // Per-transactor invariants for create are checked in
                // VaultCreate::finalizeInvariants before this runs.
                return true;
            }
            case ttVAULT_SET: {
                bool result = true;

                XRPL_ASSERT(
                    !data_.beforeVault().empty(),
                    "xrpl::ValidVault::finalize : set updated a vault");
                auto const& beforeVault = data_.beforeVault()[0];

                auto const vaultDeltaAssets = data_.deltaAssets(afterVault.pseudoId);
                if (vaultDeltaAssets)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: set must not change vault balance";
                    result = false;
                }

                if (beforeVault.assetsTotal != afterVault.assetsTotal)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: set must not change assets "
                        "outstanding";
                    result = false;
                }

                if (afterVault.assetsMaximum > kZero &&
                    afterVault.assetsTotal > afterVault.assetsMaximum)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: set assets outstanding must not "
                        "exceed assets maximum";
                    result = false;
                }

                if (beforeVault.assetsAvailable != afterVault.assetsAvailable)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: set must not change assets "
                        "available";
                    result = false;
                }

                if (beforeShares && updatedShares &&
                    beforeShares->sharesTotal != updatedShares->sharesTotal)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: set must not change shares "
                        "outstanding";
                    result = false;
                }

                return result;
            }
            case ttVAULT_DEPOSIT: {
                bool result = true;

                XRPL_ASSERT(
                    !data_.beforeVault().empty(),
                    "xrpl::ValidVault::finalize : deposit updated a vault");
                auto const& beforeVault = data_.beforeVault()[0];

                auto const maybeVaultDeltaAssets = data_.deltaAssets(afterVault.pseudoId);
                if (!maybeVaultDeltaAssets)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: deposit must change vault balance";
                    return false;  // That's all we can do
                }

                // Get the posterior scale to round calculations to
                auto const minScale =
                    data_.computeVaultMinScale(*maybeVaultDeltaAssets, view.rules());

                auto const vaultDeltaAssets =
                    roundToAsset(vaultAsset, maybeVaultDeltaAssets->delta, minScale);
                auto const txAmount = roundToAsset(vaultAsset, tx[sfAmount], minScale);

                if (vaultDeltaAssets > txAmount)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: deposit must not change vault "
                        "balance by more than deposited amount";
                    result = false;
                }

                if (vaultDeltaAssets <= kZero)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: deposit must increase vault balance";
                    result = false;
                }

                // Any payments (including deposits) made by the issuer
                // do not change their balance, but create funds instead.
                bool const issuerDeposit = [&]() -> bool {
                    if (vaultAsset.native())
                        return false;
                    return tx[sfAccount] == vaultAsset.getIssuer();
                }();

                if (!issuerDeposit)
                {
                    auto const maybeAccDeltaAssets = data_.deltaAssetsTxAccount(tx, fee);
                    if (!maybeAccDeltaAssets)
                    {
                        JLOG(j.fatal())
                            << "Invariant failed: deposit must change depositor balance";
                        return false;
                    }
                    auto const localMinScale = std::max(
                        minScale, VaultInvariantData::computeCoarsestScale({*maybeAccDeltaAssets}));

                    auto const accountDeltaAssets =
                        roundToAsset(vaultAsset, maybeAccDeltaAssets->delta, localMinScale);
                    auto const localVaultDeltaAssets =
                        roundToAsset(vaultAsset, vaultDeltaAssets, localMinScale);

                    // For IOUs, if the deposit amount is not-representable at depositor trustline
                    // scale deposit amount could round to zero, giving depositor shares for no
                    // assets. Unlike withdrawal, we do not allow that.
                    if (accountDeltaAssets >= kZero)
                    {
                        JLOG(j.fatal())
                            << "Invariant failed: deposit must decrease depositor balance";
                        result = false;
                    }

                    if (localVaultDeltaAssets * -1 != accountDeltaAssets)
                    {
                        JLOG(j.fatal()) << "Invariant failed: " <<  //
                            "deposit must change vault and depositor balance by equal amount";
                        result = false;
                    }
                }

                if (afterVault.assetsMaximum > kZero &&
                    afterVault.assetsTotal > afterVault.assetsMaximum)
                {
                    JLOG(j.fatal()) << "Invariant failed: " <<  //
                        "deposit assets outstanding must not exceed assets maximum";
                    result = false;
                }

                auto const maybeAccDeltaShares = data_.deltaShares(tx[sfAccount]);
                if (!maybeAccDeltaShares)
                {
                    JLOG(j.fatal()) << "Invariant failed: deposit must change depositor shares";
                    return false;  // That's all we can do
                }
                // We don't round shares, they are integral MPT
                auto const& accountDeltaShares = *maybeAccDeltaShares;
                if (accountDeltaShares.delta <= kZero)
                {
                    JLOG(j.fatal()) << "Invariant failed: deposit must increase depositor shares";
                    result = false;
                }

                auto const maybeVaultDeltaShares = data_.deltaShares(afterVault.pseudoId);
                if (!maybeVaultDeltaShares || maybeVaultDeltaShares->delta == kZero)
                {
                    JLOG(j.fatal()) << "Invariant failed: deposit must change vault shares";
                    return false;  // That's all we can do
                }

                // We don't round shares, they are integral MPT
                auto const& vaultDeltaShares = *maybeVaultDeltaShares;
                if (vaultDeltaShares.delta * -1 != accountDeltaShares.delta)
                {
                    JLOG(j.fatal()) << "Invariant failed: " <<  //
                        "deposit must change depositor and vault shares by equal amount";
                    result = false;
                }

                auto const assetTotalDelta = roundToAsset(
                    vaultAsset, afterVault.assetsTotal - beforeVault.assetsTotal, minScale);
                if (assetTotalDelta != vaultDeltaAssets)
                {
                    JLOG(j.fatal())
                        << "Invariant failed: deposit and assets outstanding must add up";
                    result = false;
                }

                auto const assetAvailableDelta = roundToAsset(
                    vaultAsset, afterVault.assetsAvailable - beforeVault.assetsAvailable, minScale);
                if (assetAvailableDelta != vaultDeltaAssets)
                {
                    JLOG(j.fatal()) << "Invariant failed: deposit and assets available must add up";
                    result = false;
                }

                return result;
            }
            case ttVAULT_WITHDRAW: {
                bool result = true;

                XRPL_ASSERT(
                    !data_.beforeVault().empty(),
                    "xrpl::ValidVault::finalize : withdrawal updated a vault");
                auto const& beforeVault = data_.beforeVault()[0];

                auto const maybeVaultDeltaAssets = data_.deltaAssets(afterVault.pseudoId);
                if (!maybeVaultDeltaAssets)
                {
                    JLOG(j.fatal()) << "Invariant failed: withdrawal must change vault balance";
                    return false;  // That's all we can do
                }

                // Get the posterior scale to round calculations to
                auto const minScale =
                    data_.computeVaultMinScale(*maybeVaultDeltaAssets, view.rules());

                auto const vaultPseudoDeltaAssets =
                    roundToAsset(vaultAsset, maybeVaultDeltaAssets->delta, minScale);

                if (vaultPseudoDeltaAssets >= kZero)
                {
                    JLOG(j.fatal()) << "Invariant failed: withdrawal must decrease vault balance";
                    result = false;
                }

                // Any payments (including withdrawal) going to the issuer
                // do not change their balance, but destroy funds instead.
                bool const issuerWithdrawal = [&]() -> bool {
                    if (vaultAsset.native())
                        return false;
                    auto const destination = tx[~sfDestination].value_or(tx[sfAccount]);
                    return destination == vaultAsset.getIssuer();
                }();

                if (!issuerWithdrawal)
                {
                    auto const maybeAccDelta = data_.deltaAssetsTxAccount(tx, fee);
                    auto const maybeOtherAccDelta = [&]() -> std::optional<DeltaInfo> {
                        if (auto const destination = tx[~sfDestination];
                            destination && *destination != tx[sfAccount])
                            return data_.deltaAssets(*destination);
                        return std::nullopt;
                    }();

                    if (maybeAccDelta.has_value() == maybeOtherAccDelta.has_value())
                    {
                        JLOG(j.fatal()) <<  //
                            "Invariant failed: withdrawal must change one destination balance";
                        return false;
                    }

                    auto const destinationDelta =  //
                        maybeAccDelta ? *maybeAccDelta : *maybeOtherAccDelta;

                    // the scale of destinationDelta can be coarser than
                    // minScale, so we take that into account when rounding
                    auto const destinationScale =
                        VaultInvariantData::computeCoarsestScale({destinationDelta});
                    auto const localMinScale = std::max(minScale, destinationScale);

                    auto const roundedDestinationDelta =
                        roundToAsset(vaultAsset, destinationDelta.delta, localMinScale);

                    // Post-fixCleanup3_2_0: Tolerate zero-rounded destination deltas for IOUs only.
                    // If the receiver's trust line sits at a coarser scale, the inflow may
                    // safely round down to zero.
                    //
                    // XRP and MPT remain strict. Because they are integer-exact, a zero
                    // destination delta indicates a true accounting bug, not a rounding artifact.
                    bool const tolerateZeroDelta =
                        view.rules().enabled(fixCleanup3_2_0) && !vaultAsset.integral();
                    auto const invalidBalanceChange = tolerateZeroDelta
                        ? roundedDestinationDelta < kZero
                        : roundedDestinationDelta <= kZero;
                    if (invalidBalanceChange)
                    {
                        JLOG(j.fatal()) <<  //
                            "Invariant failed: withdrawal must increase destination balance";
                        result = false;
                    }

                    auto const localPseudoDeltaAssets =
                        roundToAsset(vaultAsset, vaultPseudoDeltaAssets, localMinScale);
                    // For IOU assets near a precision boundary the destination's STAmount
                    // exponent can shift, making part of the sent value unrepresentable at the
                    // receiver's new scale — that portion is irreversibly absorbed by the IOU
                    // rail.  Tolerate the mismatch only when the destroyed amount (vault outflow
                    // minus destination inflow, in Number space) is itself sub-ULP at the
                    // destination's scale.  Floor rounding is used so that values exactly at the
                    // step boundary are not mistakenly dismissed.  Any representable discrepancy
                    // indicates a real accounting bug and must be caught.
                    auto const destroyedIsSubUlp = tolerateZeroDelta &&
                        roundToAsset(
                            vaultAsset,
                            maybeVaultDeltaAssets->delta * -1 - destinationDelta.delta,
                            destinationScale,
                            Number::RoundingMode::Downward) == kZero;
                    if (!destroyedIsSubUlp &&
                        localPseudoDeltaAssets * -1 != roundedDestinationDelta)
                    {
                        JLOG(j.fatal()) << "Invariant failed: " <<  //
                            "withdrawal must change vault and destination balance by equal "
                            "amount";
                        result = false;
                    }
                }

                // We don't round shares, they are integral MPT
                auto const accountDeltaShares = data_.deltaShares(tx[sfAccount]);
                if (!accountDeltaShares)
                {
                    JLOG(j.fatal()) << "Invariant failed: withdrawal must change depositor shares";
                    return false;
                }

                if (accountDeltaShares->delta >= kZero)
                {
                    JLOG(j.fatal())
                        << "Invariant failed: withdrawal must decrease depositor shares";
                    result = false;
                }

                // We don't round shares, they are integral MPT
                auto const vaultDeltaShares = data_.deltaShares(afterVault.pseudoId);
                if (!vaultDeltaShares || vaultDeltaShares->delta == kZero)
                {
                    JLOG(j.fatal()) << "Invariant failed: withdrawal must change vault shares";
                    return false;  // That's all we can do
                }

                if (vaultDeltaShares->delta * -1 != accountDeltaShares->delta)
                {
                    JLOG(j.fatal()) << "Invariant failed: " <<  //
                        "withdrawal must change depositor and vault shares by equal amount";
                    result = false;
                }

                auto const assetTotalDelta = roundToAsset(
                    vaultAsset, afterVault.assetsTotal - beforeVault.assetsTotal, minScale);
                // Note, vaultBalance is negative (see check above)
                if (assetTotalDelta != vaultPseudoDeltaAssets)
                {
                    JLOG(j.fatal())
                        << "Invariant failed: withdrawal and assets outstanding must add up";
                    result = false;
                }

                auto const assetAvailableDelta = roundToAsset(
                    vaultAsset, afterVault.assetsAvailable - beforeVault.assetsAvailable, minScale);

                if (assetAvailableDelta != vaultPseudoDeltaAssets)
                {
                    JLOG(j.fatal())
                        << "Invariant failed: withdrawal and assets available must add up";
                    result = false;
                }

                return result;
            }
            case ttVAULT_CLAWBACK: {
                bool result = true;

                XRPL_ASSERT(
                    !data_.beforeVault().empty(),
                    "xrpl::ValidVault::finalize : clawback updated a vault");
                auto const& beforeVault = data_.beforeVault()[0];

                if (vaultAsset.native() || vaultAsset.getIssuer() != tx[sfAccount])
                {
                    // The owner can use clawback to force-burn shares when the
                    // vault is empty but there are outstanding shares
                    if (!(beforeShares && beforeShares->sharesTotal > 0 &&
                          VaultInvariantData::isVaultEmpty(beforeVault) &&
                          beforeVault.owner == tx[sfAccount]))
                    {
                        JLOG(j.fatal()) << "Invariant failed: " <<  //
                            "clawback may only be performed by the asset issuer, or by the vault "
                            "owner of an empty vault";
                        return false;  // That's all we can do
                    }
                }

                auto const maybeVaultDeltaAssets = data_.deltaAssets(afterVault.pseudoId);
                if (maybeVaultDeltaAssets)
                {
                    auto const minScale =
                        data_.computeVaultMinScale(*maybeVaultDeltaAssets, view.rules());
                    auto const vaultDeltaAssets =
                        roundToAsset(vaultAsset, maybeVaultDeltaAssets->delta, minScale);
                    if (vaultDeltaAssets >= kZero)
                    {
                        JLOG(j.fatal()) << "Invariant failed: clawback must decrease vault balance";
                        result = false;
                    }

                    auto const assetsTotalDelta = roundToAsset(
                        vaultAsset, afterVault.assetsTotal - beforeVault.assetsTotal, minScale);
                    if (assetsTotalDelta != vaultDeltaAssets)
                    {
                        JLOG(j.fatal()) <<  //
                            "Invariant failed: clawback and assets outstanding must add up";
                        result = false;
                    }

                    auto const assetAvailableDelta = roundToAsset(
                        vaultAsset,
                        afterVault.assetsAvailable - beforeVault.assetsAvailable,
                        minScale);
                    if (assetAvailableDelta != vaultDeltaAssets)
                    {
                        JLOG(j.fatal()) <<  //
                            "Invariant failed: clawback and assets available must add up";
                        result = false;
                    }
                }
                else if (!VaultInvariantData::isVaultEmpty(beforeVault))
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: clawback must change vault balance";
                    return false;  // That's all we can do
                }

                // We don't need to round shares, they are integral MPT
                auto const maybeAccountDeltaShares = data_.deltaShares(tx[sfHolder]);
                if (!maybeAccountDeltaShares)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: clawback must change holder shares";
                    return false;  // That's all we can do
                }
                if (maybeAccountDeltaShares->delta >= kZero)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: clawback must decrease holder shares";
                    result = false;
                }

                // We don't need to round shares, they are integral MPT
                auto const vaultDeltaShares = data_.deltaShares(afterVault.pseudoId);
                if (!vaultDeltaShares || vaultDeltaShares->delta == kZero)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: clawback must change vault shares";
                    return false;  // That's all we can do
                }

                if (vaultDeltaShares->delta * -1 != maybeAccountDeltaShares->delta)
                {
                    JLOG(j.fatal()) << "Invariant failed: " <<  //
                        "clawback must change holder and vault shares by equal amount";
                    result = false;
                }

                return result;
            }

            case ttLOAN_SET:
            case ttLOAN_MANAGE:
            case ttLOAN_PAY:
                return true;

            default:
                // LCOV_EXCL_START
                UNREACHABLE("xrpl::ValidVault::finalize : unknown transaction type");
                return false;
                // LCOV_EXCL_STOP
        }
    }();

    if (!result)
    {
        // The comment at the top of this file starting with "assert(enforce)"
        // explains this assert.
        XRPL_ASSERT(enforce, "xrpl::ValidVault::finalize : vault invariants");
        return !enforce;
    }

    return true;
}

}  // namespace xrpl
