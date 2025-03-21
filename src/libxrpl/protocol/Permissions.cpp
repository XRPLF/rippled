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

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Permissions.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

Permission::Permission()
{
    granularPermissionMap = {
#pragma push_macro("PERMISSION")
#undef PERMISSION

#define PERMISSION(type, txType, value) {#type, type},

#include <xrpl/protocol/detail/permissions.macro>

#undef PERMISSION
#pragma pop_macro("PERMISSION")
    };

    granularTxTypeMap = {
#pragma push_macro("PERMISSION")
#undef PERMISSION

#define PERMISSION(type, txType, value) {type, txType},

#include <xrpl/protocol/detail/permissions.macro>

#undef PERMISSION
#pragma pop_macro("PERMISSION")
    };

    for (auto const& permission : granularPermissionMap)
        XRPL_ASSERT(
            permission.second > UINT16_MAX,
            "ripple::Permission::granularPermissionMap : granular permission "
            "value not greater than max uint16");
}

Permission const&
Permission::getInstance()
{
    static Permission const instance;
    return instance;
}

std::optional<std::uint32_t>
Permission::getGranularValue(std::string const& name) const
{
    auto const it = granularPermissionMap.find(name);
    if (it != granularPermissionMap.end())
        return static_cast<uint32_t>(it->second);

    return std::nullopt;
}

std::optional<TxType>
Permission::getGranularTxType(GranularPermissionType const& gpType) const
{
    auto const it = granularTxTypeMap.find(gpType);
    if (it != granularTxTypeMap.end())
        return it->second;

    return std::nullopt;
}

bool
Permission::isProhibited(std::uint32_t const& value) const
{
    if (value == ttSIGNER_LIST_SET || value == ttREGULAR_KEY_SET ||
        value == ttACCOUNT_SET || value == ttDELEGATE_SET ||
        value == ttACCOUNT_DELETE)
        return true;

    return false;
}

}  // namespace ripple