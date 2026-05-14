/** @file
 *  Implementations of `checkTxPermission` and `loadGranularPermission` —
 *  the two runtime enforcement helpers for the XRPL delegate permission
 *  system.  Public API and full behavioral contracts are documented in
 *  `xrpl/ledger/helpers/DelegateHelpers.h`.
 */
#include <xrpl/ledger/helpers/DelegateHelpers.h>
#include <xrpl/protocol/Permissions.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>

#include <memory>
#include <unordered_set>

namespace xrpl {
NotTEC
checkTxPermission(std::shared_ptr<SLE const> const& delegate, STTx const& tx)
{
    if (!delegate)
        return terNO_DELEGATE_PERMISSION;

    auto const permissionArray = delegate->getFieldArray(sfPermissions);
    // Transaction-level permissions are stored as TxType + 1.  The +1 shift
    // avoids a stored zero (ttPAYMENT == 0), keeping coarse permission values
    // in [1, 65535] and away from the granular range (> UINT16_MAX).
    auto const txPermission = tx.getTxnType() + 1;

    for (auto const& permission : permissionArray)
    {
        auto const permissionValue = permission[sfPermissionValue];
        if (permissionValue == txPermission)
            return tesSUCCESS;
    }

    return terNO_DELEGATE_PERMISSION;
}

void
loadGranularPermission(
    std::shared_ptr<SLE const> const& delegate,
    TxType const& txType,
    std::unordered_set<GranularPermissionType>& granularPermissions)
{
    if (!delegate)
        return;

    auto const permissionArray = delegate->getFieldArray(sfPermissions);
    for (auto const& permission : permissionArray)
    {
        auto const permissionValue = permission[sfPermissionValue];
        // The static_cast is safe: the ledger validator rejects any
        // sfPermissionValue that does not correspond to a registered
        // GranularPermissionType entry, so values reaching this point are
        // already trusted to be in-range.
        auto const granularValue = static_cast<GranularPermissionType>(permissionValue);
        auto const& type = Permission::getInstance().getGranularTxType(granularValue);
        if (type && *type == txType)
            granularPermissions.insert(granularValue);
    }
}

}  // namespace xrpl
