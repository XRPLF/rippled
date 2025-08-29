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

#ifndef RIPPLE_APP_MISC_FIREWALLUTILS_H_INCLUDED
#define RIPPLE_APP_MISC_FIREWALLUTILS_H_INCLUDED

#include <xrpld/ledger/ApplyView.h>
#include <xrpld/ledger/ReadView.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Firewall.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

namespace ripple {

/**
 * Check if a ledger entry type has firewall protection available.
 * @param type The ledger entry type.
 * @return true if firewall protection is available for this type.
 */
bool
hasFirewallProtection(LedgerEntryType const& type);

/**
 * Get all fields protected by firewalls for a ledger entry type.
 * @param type The ledger entry type.
 * @return vector of protected fields.
 */
std::vector<SField const*>
getProtectedFields(LedgerEntryType const& type);

/**
 * Check if a specific field is protected by firewall for a ledger entry type.
 * @param type The ledger entry type.
 * @param field The field to check.
 * @return true if the field is protected.
 */
bool
isFieldProtected(LedgerEntryType const& type, SField const& field);

std::vector<STObject>
getFirewallRules(STArray const& rules, LedgerEntryType const& type);

template <typename T>
inline bool
evaluateComparison(T const& left, T const& right, std::uint16_t operatorCode)
{
    switch (operatorCode)
    {
        case 1:
            return left < right;
        case 2:
            return left <= right;
        case 3:
            return left == right;
        case 4:
            return left >= right;
        case 5:
            return left > right;
        default:
            return left <= right;
    }
}

}  // namespace ripple

#endif  // RIPPLE_APP_MISC_FIREWALLUTILS_H_INCLUDED