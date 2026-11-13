#include <xrpl/ledger/entries/DelegateEntry.h>

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Permissions.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>

#include <unordered_set>

namespace xrpl {

template <typename ViewT>
NotTEC
DelegateEntry<ViewT>::checkTxPermission(STTx const& tx) const
{
    if (!this->exists())
        return terNO_DELEGATE_PERMISSION;

    auto const& sle = this->operator*();
    auto const permissionArray = sle.getFieldArray(sfPermissions);
    auto const txPermission = tx.getTxnType() + 1;

    for (auto const& permission : permissionArray)
    {
        auto const permissionValue = permission[sfPermissionValue];
        if (permissionValue == txPermission)
            return tesSUCCESS;
    }

    return terNO_DELEGATE_PERMISSION;
}

template <typename ViewT>
std::unordered_set<GranularPermissionType>
DelegateEntry<ViewT>::granularPermission(TxType const& type) const
{
    std::unordered_set<GranularPermissionType> granularPermissions;
    if (!this->exists())
        return granularPermissions;

    auto const& sle = this->operator*();
    auto const permissionArray = sle.getFieldArray(sfPermissions);
    for (auto const& permission : permissionArray)
    {
        auto const permissionValue = permission[sfPermissionValue];
        auto const granularValue = static_cast<GranularPermissionType>(permissionValue);
        auto const& permType = Permission::getInstance().getGranularTxType(granularValue);
        if (permType && *permType == type)
            granularPermissions.insert(granularValue);
    }

    return granularPermissions;
}

template <typename ViewT>
TER
DelegateEntry<ViewT>::removeFromLedger(AccountID const&)
    requires Base::kIsWritable
{
    if (!this->exists())
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto& sle = this->operator*();
    auto const delegator = sle[sfAccount];
    auto const delegatee = sle[sfAuthorize];

    // Remove from delegating account's owner directory
    if (!this->applyView().dirRemove(
            keylet::ownerDir(delegator), sle[sfOwnerNode], this->key(), false))
    {
        // LCOV_EXCL_START
        JLOG(this->journal().fatal()) << "Unable to delete Delegate from owner.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    // Remove from authorized account's owner directory, if present
    if (auto const optPage = sle[~sfDestinationNode])
    {
        if (!this->applyView().dirRemove(keylet::ownerDir(delegatee), *optPage, this->key(), false))
        {
            // LCOV_EXCL_START
            JLOG(this->journal().fatal()) << "Unable to delete Delegate from authorized account.";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }
    }

    // Only the delegating account's owner count was incremented on creation
    auto const sleOwner = this->applyView().peek(keylet::account(delegator));
    if (!sleOwner)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    decreaseOwnerCountForObject(
        this->applyView(), sleOwner, this->mutableRawSle(), 1, this->journal());

    this->erase();

    return tesSUCCESS;
}

template class DelegateEntry<ReadView>;
template class DelegateEntry<ApplyView>;

}  // namespace xrpl
