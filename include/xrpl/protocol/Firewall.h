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

#include <xrpl/protocol/TxFormats.h>

#include <optional>
#include <string>
#include <unordered_map>

namespace ripple {

enum FirewallAction { check, allow, block };

class Firewall
{
private:
    Firewall();

    std::unordered_map<std::uint16_t, FirewallAction> allTxs_;

public:
    static Firewall const&
    getInstance();

    Firewall(Firewall const&) = delete;
    Firewall&
    operator=(Firewall const&) = delete;

    bool
    isBlocked(std::uint16_t const& txType) const;
    bool
    isAllowed(std::uint16_t const& txType) const;
    bool
    isCheck(std::uint16_t const& txType) const;
};

}  // namespace ripple

#endif
