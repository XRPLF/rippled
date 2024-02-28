//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_PROTOCOL_PROTOCOL_H_INCLUDED
#define RIPPLE_PROTOCOL_PROTOCOL_H_INCLUDED

#include <ripple/basics/ByteUtilities.h>
#include <ripple/basics/base_uint.h>
#include <cstdint>

namespace ripple {

/** Protocol specific constants.

    This information is, implicitly, part of the protocol.

    @note Changing these values without adding code to the
          server to detect "pre-change" and "post-change"
          will result in a hard fork.

    @ingroup protocol
*/
/** Smallest legal byte size of a transaction. */
std::size_t constexpr txMinSizeBytes = 32;

/** Largest legal byte size of a transaction. */
std::size_t constexpr txMaxSizeBytes = megabytes(1);

/** The maximum number of unfunded offers to delete at once */
std::size_t constexpr unfundedOfferRemoveLimit = 1000;

/** The maximum number of expired offers to delete at once */
std::size_t constexpr expiredOfferRemoveLimit = 256;

/** The maximum number of metadata entries allowed in one transaction */
std::size_t constexpr oversizeMetaDataCap = 5200;

/** The maximum number of entries per directory page */
std::size_t constexpr dirNodeMaxEntries = 32;

/** The maximum number of pages allowed in a directory */
std::uint64_t constexpr dirNodeMaxPages = 262144;

/** The maximum number of items in an NFT page */
std::size_t constexpr dirMaxTokensPerPage = 32;

/** The maximum number of owner directory entries for account to be deletable */
std::size_t constexpr maxDeletableDirEntries = 1000;

/** The maximum number of token offers that can be canceled at once */
std::size_t constexpr maxTokenOfferCancelCount = 500;

/** The maximum number of offers in an offer directory for NFT to be burnable */
std::size_t constexpr maxDeletableTokenOfferEntries = 500;

/** The maximum token transfer fee allowed.

    Token transfer fees can range from 0% to 50% and are specified in tenths of
    a basis point; that is a value of 1000 represents a transfer fee of 1% and
    a value of 10000 represents a transfer fee of 10%.

    Note that for extremely low transfer fees values, it is possible that the
    calculated fee will be 0.
 */
std::uint16_t constexpr maxTransferFee = 50000;

/** The maximum length of a URI inside an NFT */
std::size_t constexpr maxTokenURILength = 256;

/** The maximum length of a Data element inside a DID */
std::size_t constexpr maxDIDDocumentLength = 256;

/** The maximum length of a URI inside a DID */
std::size_t constexpr maxDIDURILength = 256;

/** The maximum length of an Attestation inside a DID */
std::size_t constexpr maxDIDAttestationLength = 256;

/** The maximum length of a domain */
std::size_t constexpr maxDomainLength = 256;

/** A ledger index. */
using LedgerIndex = std::uint32_t;

/** A transaction identifier.
    The value is computed as the hash of the
    canonicalized, serialized transaction object.
*/
using TxID = uint256;

/** The maximum number of trustlines to delete as part of AMM account
 * deletion cleanup.
 */
std::uint16_t constexpr maxDeletableAMMTrustLines = 512;

/** The maximum length of a URI inside an Oracle */
std::size_t constexpr maxOracleURI = 256;

/** The maximum length of a Provider inside an Oracle */
std::size_t constexpr maxOracleProvider = 256;

/** The maximum size of a data series array inside an Oracle */
std::size_t constexpr maxOracleDataSeries = 10;

/** The maximum length of a SymbolClass inside an Oracle */
std::size_t constexpr maxOracleSymbolClass = 16;

/** The maximum allowed time difference between lastUpdateTime and the time
    of the last closed ledger
*/
std::size_t constexpr maxLastUpdateTimeDelta = 300;

/** The maximum price scaling factor
 */
std::size_t constexpr maxPriceScale = 20;

/** The maximum percentage of outliers to trim
 */
std::size_t constexpr maxTrim = 25;

}  // namespace ripple

#endif
