#include <xrpld/app/misc/DelegateUtils.h>

#include <xrpl/protocol/STArray.h>

namespace ripple {
NotTEC
checkTxPermission(std::shared_ptr<SLE const> const& delegate, STTx const& tx)
{
    if (!delegate)
        return terNO_DELEGATE_PERMISSION;  // LCOV_EXCL_LINE

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

void
loadGranularPermission(
    std::shared_ptr<SLE const> const& delegate,
    TxType const& txType,
    std::unordered_set<GranularPermissionType>& granularPermissions)
{
    if (!delegate)
        return;  // LCOV_EXCL_LINE

    auto const permissionArray = delegate->getFieldArray(sfPermissions);
    for (auto const& permission : permissionArray)
    {
        auto const permissionValue = permission[sfPermissionValue];
        auto const granularValue =
            static_cast<GranularPermissionType>(permissionValue);
        auto const& type =
            Permission::getInstance().getGranularTxType(granularValue);
        if (type && *type == txType)
            granularPermissions.insert(granularValue);
    }
}

}  // namespace ripple
