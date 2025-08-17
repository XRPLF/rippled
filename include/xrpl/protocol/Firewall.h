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

#ifndef RIPPLE_PROTOCOL_FIREWALL_H_INCLUDED
#define RIPPLE_PROTOCOL_FIREWALL_H_INCLUDED

#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/TER.h>

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

namespace ripple {

enum class FirewallOperator : std::uint8_t {
    LESS_THAN = 1,
    LESS_THAN_EQUAL = 2,
    EQUAL = 3,
    GREATER_THAN_EQUAL = 4,
    GREATER_THAN = 5
};

/**
 * Firewall types for different ledger entries
 */
enum FirewallType : std::uint32_t {
#pragma push_macro("FIREWALL_ENTRY")
#undef FIREWALL_ENTRY

#define FIREWALL_ENTRY(name, ledgerType, fields, value) name = value,

#include <xrpl/protocol/detail/firewall.macro>

#undef FIREWALL_ENTRY
#pragma pop_macro("FIREWALL_ENTRY")
};

class Firewall
{
private:
    Firewall();

    std::unordered_map<std::string, FirewallType> firewallNameMap_;
    std::unordered_map<FirewallType, std::string> firewallTypeNameMap_;
    std::unordered_map<FirewallType, LedgerEntryType> firewallLedgerTypeMap_;
    std::unordered_map<FirewallType, std::vector<SField const*>>
        firewallFieldsMap_;

public:
    static Firewall const&
    getInstance();

    Firewall(Firewall const&) = delete;
    Firewall&
    operator=(Firewall const&) = delete;

    std::optional<FirewallType>
    getFirewallType(std::string const& name) const;

    std::optional<std::string>
    getFirewallName(FirewallType const& type) const;

    std::optional<LedgerEntryType>
    getLedgerType(FirewallType const& type) const;

    std::optional<std::vector<SField const*>>
    getFields(FirewallType const& type) const;

    std::vector<SField const*>
    getFieldsForLedgerType(LedgerEntryType const& ledgerType) const;

    bool
    handlesField(LedgerEntryType const& ledgerType, SField const& field) const;

    bool
    hasFirewall(LedgerEntryType const& ledgerType) const;
};

}  // namespace ripple

#endif