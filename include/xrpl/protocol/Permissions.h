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

#ifndef RIPPLE_PROTOCOL_PERMISSION_H_INCLUDED
#define RIPPLE_PROTOCOL_PERMISSION_H_INCLUDED

#include <xrpl/protocol/TxFormats.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace ripple {
/**
 * We have both transaction type permissions and granular type permissions.
 * Since we will reuse the TransactionFormats to parse the Transaction
 * Permissions, only the GranularPermissionType is defined here. To prevent
 * conflicts with TxType, the GranularPermissionType is always set to a value
 * greater than the maximum value of uint16.
 */
enum GranularPermissionType : std::uint32_t {
#pragma push_macro("PERMISSION")
#undef PERMISSION

#define PERMISSION(type, txType, value) type = value,

#include <xrpl/protocol/detail/permissions.macro>

#undef PERMISSION
#pragma pop_macro("PERMISSION")
};

class Permission
{
private:
    Permission();

    std::unordered_map<std::string, GranularPermissionType>
        granularPermissionMap;

    std::unordered_map<GranularPermissionType, std::string> granularNameMap;

    std::unordered_map<GranularPermissionType, TxType> granularTxTypeMap;

    std::unordered_set<std::string> supportedTransactions;

public:
    static Permission const&
    getInstance();

    Permission(const Permission&) = delete;
    Permission&
    operator=(const Permission&) = delete;

    std::optional<std::uint32_t>
    getGranularValue(std::string const& name) const;

    std::optional<std::string>
    getGranularName(GranularPermissionType const& value) const;

    std::optional<TxType>
    getGranularTxType(GranularPermissionType const& gpType) const;

    bool
    isProhibited(std::uint32_t const& value) const;
};

}  // namespace ripple

#endif
