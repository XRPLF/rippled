//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

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

#ifndef RIPPLE_RPC_CTID_H_INCLUDED
#define RIPPLE_RPC_CTID_H_INCLUDED

#include <boost/algorithm/string/predicate.hpp>
#include <boost/regex.hpp>
#include <optional>
#include <regex>
#include <sstream>

namespace ripple {

namespace RPC {

// CTID stands for Concise Transaction ID.
//
// The CTID comes from XLS-15d: Concise Transaction Identifier #34
//
//   https://github.com/XRPLF/XRPL-Standards/discussions/34
//
// The Concise Transaction ID provides a way to identify a transaction
// that includes which network the transaction was submitted to.

inline std::optional<std::string>
encodeCTID(uint32_t ledgerSeq, uint32_t txnIndex, uint32_t networkID) noexcept
{
    if (ledgerSeq > 0x0FFF'FFFF || txnIndex > 0xFFFF || networkID > 0xFFFF)
        return {};

    uint64_t ctidValue =
        ((0xC000'0000ULL + static_cast<uint64_t>(ledgerSeq)) << 32) +
        (static_cast<uint64_t>(txnIndex) << 16) + networkID;

    std::stringstream buffer;
    buffer << std::hex << std::uppercase << std::setfill('0') << std::setw(16)
           << ctidValue;
    return {buffer.str()};
}

template <typename T>
inline std::optional<std::tuple<uint32_t, uint16_t, uint16_t>>
decodeCTID(const T ctid) noexcept
{
    uint64_t ctidValue{0};
    if constexpr (
        std::is_same_v<T, std::string> || std::is_same_v<T, char*> ||
        std::is_same_v<T, const char*> || std::is_same_v<T, std::string_view>)
    {
        std::string const ctidString(ctid);

        if (ctidString.length() != 16)
            return {};

        if (!boost::regex_match(ctidString, boost::regex("^[0-9A-F]+$")))
            return {};

        ctidValue = std::stoull(ctidString, nullptr, 16);
    }
    else if constexpr (std::is_integral_v<T>)
        ctidValue = ctid;
    else
        return {};

    if ((ctidValue & 0xF000'0000'0000'0000ULL) != 0xC000'0000'0000'0000ULL)
        return {};

    uint32_t ledger_seq = (ctidValue >> 32) & 0xFFFF'FFFUL;
    uint16_t txn_index = (ctidValue >> 16) & 0xFFFFU;
    uint16_t network_id = ctidValue & 0xFFFFU;
    return {{ledger_seq, txn_index, network_id}};
}

}  // namespace RPC
}  // namespace ripple

#endif
