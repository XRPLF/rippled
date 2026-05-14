/**
 * @file CTID.h
 * @brief Concise Transaction ID (CTID) encoding and decoding (XLS-15d).
 *
 * A CTID uniquely identifies a transaction by its position within a ledger on
 * a specific network.  Unlike a 64-character transaction hash, a CTID encodes
 * three coordinates — ledger sequence, transaction index, and network ID — in
 * a single 16-character uppercase hex string, making it compact and
 * unambiguous across networks.
 *
 * ## Binary layout (64-bit value, 16 hex digits)
 * ```
 * [4 bits: 0xC] [28 bits: ledgerSeq] [16 bits: txnIndex] [16 bits: networkID]
 * ```
 * The top nibble is always `C` (0b1100), acting as a magic prefix that
 * distinguishes a CTID from an arbitrary hex string.  Valid CTIDs therefore
 * satisfy `(value & 0xF000'0000'0000'0000) == 0xC000'0000'0000'0000`.
 *
 * Practical capacity: ~268 million ledgers, 65,536 transactions per ledger,
 * 65,536 distinct networks.
 *
 * @see https://github.com/XRPLF/XRPL-Standards/discussions/34
 * @see encodeCTID, decodeCTID
 */

#pragma once

#include <boost/regex.hpp>

#include <optional>
#include <regex>
#include <sstream>

namespace xrpl::RPC {

/**
 * Encodes a transaction's position in a ledger as a Concise Transaction ID
 * (CTID) string.
 *
 * The resulting 16-character uppercase hex string encodes the three inputs
 * using the layout `[4-bit 0xC magic][28-bit ledgerSeq][16-bit txnIndex]
 * [16-bit networkID]`.  The output is zero-padded to exactly 16 characters,
 * which the decoder relies on as its first validation step.
 *
 * Out-of-range inputs return `std::nullopt` immediately rather than truncating
 * silently — silent truncation would produce a valid-looking CTID pointing to
 * the wrong transaction.
 *
 * @param ledgerSeq Ledger sequence number.  Must be ≤ 0x0FFF'FFFF (28 bits).
 * @param txnIndex  Transaction index within the ledger.  Must be ≤ 0xFFFF.
 * @param networkID Network identifier.  Must be ≤ 0xFFFF.
 * @return The CTID as a 16-character uppercase hex string, or `std::nullopt`
 *     if any argument exceeds its allowed range.
 */
inline std::optional<std::string>
encodeCTID(uint32_t ledgerSeq, uint32_t txnIndex, uint32_t networkID) noexcept
{
    constexpr uint32_t kMAX_LEDGER_SEQ = 0x0FFF'FFFF;
    constexpr uint32_t kMAX_TXN_INDEX = 0xFFFF;
    constexpr uint32_t kMAX_NETWORK_ID = 0xFFFF;

    if (ledgerSeq > kMAX_LEDGER_SEQ || txnIndex > kMAX_TXN_INDEX || networkID > kMAX_NETWORK_ID)
        return std::nullopt;

    uint64_t const ctidValue = ((0xC000'0000ULL + static_cast<uint64_t>(ledgerSeq)) << 32) |
        ((static_cast<uint64_t>(txnIndex) << 16) | networkID);

    std::stringstream buffer;
    buffer << std::hex << std::uppercase << std::setfill('0') << std::setw(16) << ctidValue;
    return buffer.str();
}

/**
 * Decodes a Concise Transaction ID (CTID) into its component ledger sequence,
 * transaction index, and network ID.
 *
 * The template accepts string-like types (`std::string`, `std::string_view`,
 * `char*`, `const char*`) and integral types via `if constexpr` dispatch.
 * Passing an unsupported type safely returns `std::nullopt` rather than
 * producing a compile error.
 *
 * **String path:** two guards fire before numeric conversion — an exact
 * 16-character length check, then a `boost::regex` match against
 * `^[0-9A-Fa-f]{16}$` (case-insensitive hex).  `boost::regex` is used
 * consistently with the broader XRPL codebase to avoid the poor performance
 * of some `std::regex` implementations.  The `std::stoull` call that follows
 * is wrapped in a `try/catch` that is excluded from coverage
 * (`LCOV_EXCL_START/STOP`) because the prior validation makes it unreachable
 * in practice; the guard exists for defensive correctness.
 *
 * **Integral path:** the value is cast directly to `uint64_t`, then subjected
 * to the same prefix mask check as the string path.
 *
 * After type-specific parsing the prefix mask
 * `(value & 0xF000'0000'0000'0000) == 0xC000'0000'0000'0000` is verified,
 * rejecting any value whose top nibble is not `C`.
 *
 * The return type uses `uint32_t` for `ledgerSeq` (28 usable bits fit) and
 * `uint16_t` for `txnIndex` and `networkID`, enabling compile-time range
 * reasoning by callers.
 *
 * @tparam T Input type: `std::string`, `std::string_view`, `char*`,
 *     `const char*`, or any integral type.  Other types return `std::nullopt`.
 * @param ctid The CTID value to decode.
 * @return A tuple of `(ledgerSeq, txnIndex, networkID)` on success, or
 *     `std::nullopt` if the input is the wrong length, contains non-hex
 *     characters, has an incorrect `C` prefix nibble, or is an unsupported
 *     type.
 * @note Callers receiving a decoded `networkID` should compare it against the
 *     local node's network ID and reject mismatches — a CTID from another
 *     network will decode successfully but refer to a different ledger
 *     history.
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

        static boost::regex const kHEX_REGEX("^[0-9A-Fa-f]{16}$");
        if (!boost::regex_match(ctidString, kHEX_REGEX))
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
    constexpr uint64_t kCTID_PREFIX_MASK = 0xF000'0000'0000'0000ULL;
    constexpr uint64_t kCTID_PREFIX = 0xC000'0000'0000'0000ULL;
    if ((ctidValue & kCTID_PREFIX_MASK) != kCTID_PREFIX)
        return std::nullopt;

    uint32_t const ledgerSeq = static_cast<uint32_t>((ctidValue >> 32) & 0x0FFF'FFFF);
    uint16_t const txnIndex = static_cast<uint16_t>((ctidValue >> 16) & 0xFFFF);
    uint16_t const networkID = static_cast<uint16_t>(ctidValue & 0xFFFF);

    return std::make_tuple(ledgerSeq, txnIndex, networkID);
}

}  // namespace xrpl::RPC
