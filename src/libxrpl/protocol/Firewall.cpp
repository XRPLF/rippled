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

#include <xrpl/protocol/Firewall.h>

namespace ripple {

Firewall::Firewall()
{
    // Initialize the maps using the macro definitions
#pragma push_macro("FIREWALL_ENTRY")
#undef FIREWALL_ENTRY

#define FIREWALL_ENTRY(name, ledgerType, fields, value)      \
    firewallNameMap_[#name] = FirewallType::name;            \
    firewallTypeNameMap_[FirewallType::name] = #name;        \
    firewallLedgerTypeMap_[FirewallType::name] = ledgerType; \
    firewallFieldsMap_[FirewallType::name] = std::vector<SField const*> fields;

#include <xrpl/protocol/detail/firewall.macro>

#undef FIREWALL_ENTRY
#pragma pop_macro("FIREWALL_ENTRY")
}

Firewall const&
Firewall::getInstance()
{
    static Firewall instance;
    return instance;
}

std::optional<FirewallType>
Firewall::getFirewallType(std::string const& name) const
{
    auto it = firewallNameMap_.find(name);
    if (it != firewallNameMap_.end())
        return it->second;
    return std::nullopt;
}

std::optional<std::string>
Firewall::getFirewallName(FirewallType const& type) const
{
    auto it = firewallTypeNameMap_.find(type);
    if (it != firewallTypeNameMap_.end())
        return it->second;
    return std::nullopt;
}

std::optional<LedgerEntryType>
Firewall::getLedgerType(FirewallType const& type) const
{
    auto it = firewallLedgerTypeMap_.find(type);
    if (it != firewallLedgerTypeMap_.end())
        return it->second;
    return std::nullopt;
}

std::optional<std::vector<SField const*>>
Firewall::getFields(FirewallType const& type) const
{
    auto it = firewallFieldsMap_.find(type);
    if (it != firewallFieldsMap_.end())
        return it->second;
    return std::nullopt;
}

std::vector<SField const*>
Firewall::getFieldsForLedgerType(LedgerEntryType const& ledgerType) const
{
    std::vector<SField const*> allFields;

    for (auto const& pair : firewallLedgerTypeMap_)
    {
        if (pair.second == ledgerType)
        {
            auto fieldsIt = firewallFieldsMap_.find(pair.first);
            if (fieldsIt != firewallFieldsMap_.end())
            {
                auto const& fields = fieldsIt->second;
                allFields.insert(allFields.end(), fields.begin(), fields.end());
            }
        }
    }

    return allFields;
}

bool
Firewall::handlesField(LedgerEntryType const& ledgerType, SField const& field)
    const
{
    auto fields = getFieldsForLedgerType(ledgerType);

    for (auto const* fieldPtr : fields)
    {
        if (fieldPtr && *fieldPtr == field)
            return true;
    }

    return false;
}

bool
Firewall::hasFirewall(LedgerEntryType const& ledgerType) const
{
    for (auto const& pair : firewallLedgerTypeMap_)
    {
        if (pair.second == ledgerType)
            return true;
    }
    return false;
}

}  // namespace ripple