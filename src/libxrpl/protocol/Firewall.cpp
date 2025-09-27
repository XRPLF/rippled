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
#include <xrpl/protocol/Firewall.h>
#include <xrpl/protocol/Permissions.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

Firewall::Firewall()
{
    allTxs_ = {
#pragma push_macro("TRANSACTION")
#undef TRANSACTION

#define TRANSACTION(                                                         \
    tag, value, name, delegatable, amendment, permissions, firewall, fields) \
    {value, firewall},

#include <xrpl/protocol/detail/transactions.macro>

#undef TRANSACTION
#pragma pop_macro("TRANSACTION")
    };
}

Firewall const&
Firewall::getInstance()
{
    static Firewall const instance;
    return instance;
}

bool
Firewall::isBlocked(std::uint16_t const& txType) const
{
    auto const it = allTxs_.find(txType);
    if (it != allTxs_.end())
        return it->second == FirewallAction::block;
    return false;  // LCOV_EXCL_LINE
}

bool
Firewall::isAllowed(std::uint16_t const& txType) const
{
    auto const it = allTxs_.find(txType);
    if (it != allTxs_.end())
        return it->second == FirewallAction::allow;
    return false;  // LCOV_EXCL_LINE
}

bool
Firewall::isCheck(std::uint16_t const& txType) const
{
    auto const it = allTxs_.find(txType);
    if (it != allTxs_.end())
        return it->second == FirewallAction::check;
    return false;  // LCOV_EXCL_LINE
}

}  // namespace ripple
