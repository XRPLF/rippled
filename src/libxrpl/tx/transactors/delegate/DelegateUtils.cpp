#include <xrpl/ledger/helpers/DelegateHelpers.h>
#include <xrpl/protocol/STArray.h>

namespace xrpl {
NotTEC
checkTxPermission(std::shared_ptr<SLE const> const& delegate, STTx const& tx)
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
getGranularPermission(std::shared_ptr<SLE const> const& delegate, TxType const& txType)
{
    std::unordered_set<GranularPermissionType> granularPermissions;
    if (!delegate)
        return granularPermissions;  // LCOV_EXCL_LINE

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

std::optional<std::unordered_set<GranularPermissionType>>
checkGranularPermission(std::shared_ptr<SLE const> const& delegate, STTx const& tx)
{
    auto gps = getGranularPermission(delegate, tx.getTxnType());

    if (!Permission::getInstance().checkGranularSandbox(tx, gps))
        return std::nullopt;

    return gps;
}

}  // namespace xrpl
