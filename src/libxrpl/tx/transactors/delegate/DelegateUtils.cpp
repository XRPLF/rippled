#include <xrpl/ledger/helpers/DelegateHelpers.h>
#include <xrpl/protocol/Permissions.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>

#include <unordered_set>

namespace xrpl {
NotTEC
checkTxPermission(SLE::const_ref delegate, STTx const& tx)
{
    if (!delegate)
        return terNO_DELEGATE_PERMISSION;

    auto const permissionArray = delegate->getFieldArray(sfPermissions);
    auto const txPermission = tx.getTxnType() + 1;

    for (auto const& permission : permissionArray)
    {
        auto const permissionValue = permission[sfPermissionValue];
        if (permissionValue == txPermission)
            return tesSUCCESS;
    }

    return terNO_DELEGATE_PERMISSION;
}

std::unordered_set<GranularPermissionType>
getGranularPermission(SLE::const_ref delegate, TxType const& txType)
{
    std::unordered_set<GranularPermissionType> granularPermissions;
    if (!delegate)
        return granularPermissions;

    auto const permissionArray = delegate->getFieldArray(sfPermissions);
    for (auto const& permission : permissionArray)
    {
        auto const permissionValue = permission[sfPermissionValue];
        auto const granularValue = static_cast<GranularPermissionType>(permissionValue);
        auto const& type = Permission::getInstance().getGranularTxType(granularValue);
        if (type && *type == txType)
            granularPermissions.insert(granularValue);
    }

    return granularPermissions;
}

}  // namespace xrpl
