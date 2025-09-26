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

#ifndef XRPL_RPC_CTID_H_INCLUDED
#define XRPL_RPC_CTID_H_INCLUDED

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

/**
 * @brief Encodes ledger sequence, transaction index, and network ID into a CTID
 * string.
 *
 * @param ledgerSeq  Ledger sequence number (max 0x0FFF'FFFF).
 * @param txnIndex   Transaction index within the ledger (max 0xFFFF).
 * @param networkID  Network identifier (max 0xFFFF).
 * @return Optional CTID string in uppercase hexadecimal, or std::nullopt if
 * inputs are out of range.
 */
inline std::optional<std::string>
encodeCTID(uint32_t ledgerSeq, uint32_t txnIndex, uint32_t networkID) noexcept
{
    constexpr uint32_t maxLedgerSeq = 0x0FFF'FFFF;
    constexpr uint32_t maxTxnIndex = 0xFFFF;
    constexpr uint32_t maxNetworkID = 0xFFFF;

    if (ledgerSeq > maxLedgerSeq || txnIndex > maxTxnIndex ||
        networkID > maxNetworkID)
        return std::nullopt;

    uint64_t ctidValue =
        ((0xC000'0000ULL + static_cast<uint64_t>(ledgerSeq)) << 32) |
        ((static_cast<uint64_t>(txnIndex) << 16) | networkID);

    std::stringstream buffer;
    buffer << std::hex << std::uppercase << std::setfill('0') << std::setw(16)
           << ctidValue;
    return buffer.str();
}

/**
 * @brief Decodes a CTID string or integer into its component parts.
 *
 * @tparam T  Type of the CTID input (string, string_view, char*, integral).
 * @param ctid  CTID value to decode.
 * @return Optional tuple of (ledgerSeq, txnIndex, networkID), or std::nullopt
 * if invalid.
 */
template <typename T>
inline std::optional<std::tuple<uint32_t, uint16_t, uint16_t>>
decodeCTID(T const ctid) noexcept
{
    uint64_t ctidValue = 0;

    if constexpr (
        std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view> ||
        std::is_same_v<T, char*> || std::is_same_v<T, char const*>)
    {
        std::string const ctidString(ctid);

        if (ctidString.size() != 16)
            return std::nullopt;

        static boost::regex const hexRegex("^[0-9A-Fa-f]{16}$");
        if (!boost::regex_match(ctidString, hexRegex))
            return std::nullopt;

        try
        {
            ctidValue = std::stoull(ctidString, nullptr, 16);
        }
        // LCOV_EXCL_START
        catch (...)
        {
            // should be impossible to hit given the length/regex check
            return std::nullopt;
        }
        // LCOV_EXCL_STOP
    }
    else if constexpr (std::is_integral_v<T>)
    {
        ctidValue = static_cast<uint64_t>(ctid);
    }
    else
    {
        return std::nullopt;
    }

    // Validate CTID prefix.
    constexpr uint64_t ctidPrefixMask = 0xF000'0000'0000'0000ULL;
    constexpr uint64_t ctidPrefix = 0xC000'0000'0000'0000ULL;
    if ((ctidValue & ctidPrefixMask) != ctidPrefix)
        return std::nullopt;

    uint32_t ledgerSeq = static_cast<uint32_t>((ctidValue >> 32) & 0x0FFF'FFFF);
    uint16_t txnIndex = static_cast<uint16_t>((ctidValue >> 16) & 0xFFFF);
    uint16_t networkID = static_cast<uint16_t>(ctidValue & 0xFFFF);

    return std::make_tuple(ledgerSeq, txnIndex, networkID);
}

}  // namespace RPC
}  // namespace ripple

#endif
