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

#include <xrpld/app/misc/FirewallUtils.h>

#include <xrpl/protocol/TER.h>

#include <unordered_set>

namespace ripple {

bool
hasFirewallProtection(LedgerEntryType const& type)
{
    return Firewall::getInstance().hasFirewall(type);
}

std::vector<SField const*>
getProtectedFields(LedgerEntryType const& type)
{
    return Firewall::getInstance().getFieldsForLedgerType(type);
}

bool
isFieldProtected(LedgerEntryType const& type, SField const& field)
{
    return Firewall::getInstance().handlesField(type, field);
}

std::vector<STObject>
getFirewallRules(STArray const& rules, LedgerEntryType const& type)
{
    std::vector<STObject> matchingRules;
    for (auto const& rule : rules)
    {
        if (rule.getFieldU16(sfLedgerEntryType) == type)
            matchingRules.push_back(rule);
    }
    return matchingRules;
}

}  // namespace ripple