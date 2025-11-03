//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

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
