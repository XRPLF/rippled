#include <xrpl/tx/invariants/VaultPositionsInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/VaultManagerHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <set>

namespace xrpl {

namespace {

bool
inMandate(STArray const& mandate, Asset const& asset)
{
    return std::ranges::any_of(mandate, [&](STObject const& e) { return e[sfAsset] == asset; });
}

}  // namespace

void
ValidVaultPositions::visitEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after)
{
    auto const type = before ? before->getType() : after->getType();
    auto const entry = std::pair{before, isDelete ? SLE::const_pointer{} : after};
    switch (type)
    {
        case ltVAULT_POSITION:
            positions_.push_back(entry);
            break;
        case ltVAULT_MANAGER:
            managers_.push_back(entry);
            break;
        case ltVAULT:
            vaults_.push_back(entry);
            break;
        default:
            break;
    }
}

bool
ValidVaultPositions::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    if (!isTesSuccess(result) || !view.rules().enabled(featureManagedVault))
        return true;

    auto const txType = tx.getTxnType();
    auto const now = view.parentCloseTime().time_since_epoch().count();
    bool ok = true;
    auto const fail = [&](char const* what) {
        JLOG(j.fatal()) << "Invariant failed: " << what;
        ok = false;
    };

    std::set<uint256> vaultIDs;
    for (auto const& [before, after] : positions_)
    {
        auto const& any = after ? after : before;
        vaultIDs.insert(any->at(sfVaultID));
        if (!before && txType != ttVAULT_TRADE)
            fail("position created outside VaultTrade");
        if (!after && txType != ttVAULT_TRADE)
            fail("position deleted outside VaultTrade");
        if (!after)
            continue;

        if (after->at(sfMarkPrice) <= Number{})
            fail("position mark price must be positive");
        if (after->at(sfCostBasis) < Number{})
            fail("position cost basis is negative");
        if (before)
        {
            if (after->at(sfLastMarkTime) < before->at(sfLastMarkTime))
                fail("position mark time moved backwards");
            bool const marked = after->at(sfLastMarkTime) != before->at(sfLastMarkTime) ||
                after->at(sfMarkPrice) != before->at(sfMarkPrice);
            if (marked && txType != ttVAULT_MARK)
                fail("position mark changed outside VaultMark");
            if (after->at(sfCostBasis) != before->at(sfCostBasis) && txType != ttVAULT_TRADE)
                fail("position cost basis changed outside VaultTrade");
        }

        auto const manager = view.read(keylet::vaultManager(after->at(sfVaultID)));
        if (!manager || !inMandate(manager->getFieldArray(sfMandate), after->at(sfAsset)))
            fail("position asset is not in the mandate");
    }

    for (auto const& [before, after] : vaults_)
    {
        auto const& any = after ? after : before;
        if (!after || !before)
            continue;
        if (after->at(sfMarkHaltUntil) < before->at(sfMarkHaltUntil))
            fail("vault halt moved backwards");
        if (after->at(sfMarkHaltUntil) != before->at(sfMarkHaltUntil) && txType != ttVAULT_MARK &&
            txType != ttVAULT_MANAGER_SET)
            fail("vault halt changed outside VaultMark or VaultManagerSet");
        if (view.exists(keylet::vaultManager(any->key())))
            vaultIDs.insert(any->key());
    }

    for (auto const& [before, after] : managers_)
    {
        if (!after)
            continue;
        vaultIDs.insert(after->at(sfVaultID));

        // The roles stay disjoint.
        AccountID const manager = after->at(sfManager);
        AccountID const valuer = after->at(sfValuer);
        AccountID const guardian = after->at(sfGuardian);
        auto const vault = view.read(keylet::vault(after->at(sfVaultID)));
        if (manager == valuer || manager == guardian || valuer == guardian ||
            (vault && (valuer == vault->at(sfOwner) || guardian == vault->at(sfOwner))))
            fail("vault roles are not disjoint");

        // A role changes only by its pending change, once due.
        if (before)
        {
            for (auto const* role : {&sfManager, &sfValuer, &sfGuardian})
            {
                if (after->at(*role) == before->at(*role))
                    continue;
                bool const due = before->isFieldPresent(sfPendingUpdate) &&
                    now >= before->at(sfPendingUntil) &&
                    before->getFieldObject(sfPendingUpdate)[~*role] == after->at(*role);
                if (!due)
                    fail("roles changed before the timelock");
            }
        }

        // Only a loosening waits.
        if (after->isFieldPresent(sfPendingUpdate))
        {
            auto const& pending = after->getFieldObject(sfPendingUpdate);
            for (auto const* field : std::array<SField const*, 9>{
                     &sfDeployedMaximum,
                     &sfCoverRateMinimum,
                     &sfCoverRateLiquidation,
                     &sfManagementFeeRate,
                     &sfPerformanceFeeRate,
                     &sfMarkMoveLimit,
                     &sfMarkHaltPeriod,
                     &sfMarkStaleAfter,
                     &sfTimelock})
            {
                if (pending.isFieldPresent(*field) && !isLooseningField(*field, *after, pending))
                    fail("tightening placed in the pending update");
            }
            if (pending.isFieldPresent(sfMandate) &&
                !isLooseningMandate(
                    after->getFieldArray(sfMandate), pending.getFieldArray(sfMandate)))
                fail("tightening placed in the pending update");
        }
    }

    // Every vault touched: its count matches its positions.
    for (auto const& vaultID : vaultIDs)
    {
        auto const vault = view.read(keylet::vault(vaultID));
        auto const manager = view.read(keylet::vaultManager(vaultID));
        if (!vault || !manager)
            continue;
        std::uint32_t count = 0;
        for (auto const& entry : manager->getFieldArray(sfMandate))
        {
            if (view.exists(keylet::vaultPosition(vaultID, entry[sfAsset])))
                ++count;
        }
        if (count != vault->at(sfManagedPositionCount))
            fail("vault position count does not match its positions");
    }

    // A role-holder never deals in the vault it serves.
    if ((txType == ttVAULT_DEPOSIT || txType == ttVAULT_WITHDRAW) &&
        isVaultRoleHolder(view, tx[sfVaultID], tx[sfAccount]))
        fail("role-holder dealt in the vault");

    if (txType == ttVAULT_TRADE)
    {
        auto const vaultID = tx[sfVaultID];
        auto const vault = view.read(keylet::vault(vaultID));
        auto const manager = view.read(keylet::vaultManager(vaultID));
        if (vault && manager)
        {
            Asset const vaultAsset = vault->at(sfAsset);
            bool const open = tx[sfSendMax].asset() == vaultAsset;
            Asset const asset = open ? tx[sfAmount].asset() : tx[sfSendMax].asset();
            auto const position = view.read(keylet::vaultPosition(vaultID, asset));
            if (position && positionHolding(view, vault, asset) == beast::kZero)
                fail("trade left a position with no holding");

            if (open && position)
            {
                Number const deployed = vaultPositionsValue(view, vault);
                if (deployed > manager->at(sfDeployedMaximum))
                    fail("open exceeds the deployed maximum");
                for (auto const& entry : manager->getFieldArray(sfMandate))
                {
                    Number const cap = entry[~sfPositionMaximum].value_or(Number{});
                    if (entry[sfAsset] == asset && cap != Number{} &&
                        positionValue(view, vault, position) > cap)
                        fail("open exceeds the position maximum");
                }
                if (minimumManagerCover(deployed, *manager) > manager->at(sfCoverAvailable))
                    fail("open leaves cover below the minimum");
            }
        }
    }

    return ok;
}

}  // namespace xrpl
