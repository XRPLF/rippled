#pragma once

#include <xrpl/beast/hash/hash_append.h>

#include <cstdint>

namespace xrpl {

namespace detail {

constexpr std::uint32_t
makeHashPrefix(char a, char b, char c)
{
    return (static_cast<std::uint32_t>(a) << 24) + (static_cast<std::uint32_t>(b) << 16) +
        (static_cast<std::uint32_t>(c) << 8);
}

}  // namespace detail

/** Prefix for hashing functions.

    These prefixes are inserted before the source material used to generate
    various hashes. This is done to put each hash in its own "space." This way,
    two different types of objects with the same binary data will produce
    different hashes.

    Each prefix is a 4-byte value with the last byte set to zero and the first
    three bytes formed from the ASCII equivalent of some arbitrary string. For
    example "TXN".

    @note Hash prefixes are part of the protocol; you cannot, arbitrarily,
          change the type or the value of any of these without causing breakage.
*/
enum class HashPrefix : std::uint32_t {
    /** transaction plus signature to give transaction ID */
    TransactionId = detail::makeHashPrefix('T', 'X', 'N'),

    /** transaction plus metadata */
    TxNode = detail::makeHashPrefix('S', 'N', 'D'),

    /** account state */
    LeafNode = detail::makeHashPrefix('M', 'L', 'N'),

    /** inner node in V1 tree */
    InnerNode = detail::makeHashPrefix('M', 'I', 'N'),

    /** ledger master data for signing */
    LedgerMaster = detail::makeHashPrefix('L', 'W', 'R'),

    /** inner transaction to sign */
    TxSign = detail::makeHashPrefix('S', 'T', 'X'),

    /** inner transaction to multi-sign */
    TxMultiSign = detail::makeHashPrefix('S', 'M', 'T'),

    /** validation for signing */
    Validation = detail::makeHashPrefix('V', 'A', 'L'),

    /** proposal for signing */
    Proposal = detail::makeHashPrefix('P', 'R', 'P'),

    /** Manifest */
    Manifest = detail::makeHashPrefix('M', 'A', 'N'),

    /** Payment Channel Claim */
    PaymentChannelClaim = detail::makeHashPrefix('C', 'L', 'M'),

    /** Batch */
    Batch = detail::makeHashPrefix('B', 'C', 'H'),
};

template <class Hasher>
void
hash_append(Hasher& h, HashPrefix const& hp) noexcept
{
    using beast::hash_append;
    hash_append(h, static_cast<std::uint32_t>(hp));
}

}  // namespace xrpl
