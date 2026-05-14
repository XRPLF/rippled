#pragma once

#include <xrpl/basics/base_uint.h>

#include <string_view>

namespace xrpl::nft {

/** Bitmask that separates the owner-identity region of an NFToken page key
 *  from its token-ordering region.
 *
 *  Every `NFTokenID` encodes the issuing `AccountID` in its high 160 bits and
 *  token-specific data (taxon + sequence) in its low 96 bits. Ledger page keys
 *  for `ltNFTOKEN_PAGE` SLEs fuse those two regions into a single `uint256`.
 *  `kPAGE_MASK` — 96 bits of `1` in the least-significant position, 160 bits
 *  of `0` above — is the canonical tool for splitting them:
 *
 *  - `token & kPAGE_MASK`   → low 96 bits (token-ordering portion)
 *  - `key   & ~kPAGE_MASK`  → high 160 bits (owner AccountID portion)
 *
 *  Typical uses:
 *  - **Page key construction** (`Indexes.cpp`): `(k.key & ~kPAGE_MASK) + (token & kPAGE_MASK)`
 *  - **Max-page sentinel** (`keylet::nftpage_max`): initialize the full `uint256`
 *    to `kPAGE_MASK` (all-ones in the low 96 bits), then overwrite the high
 *    bytes with the owner's `AccountID` to form the lexicographically largest
 *    page key for that owner.
 *  - **RPC pagination** (`AccountNFTs.cpp`, `AccountObjects.cpp`): validate
 *    that a client-supplied marker or `entryIndex` belongs to the correct page.
 *  - **Sort-order comparisons** (`NFTokenHelpers.cpp`): tokens with different
 *    low-96-bit values belong to different pages.
 *  - **Invariant checks** (`NFTInvariant.cpp`): verify that a page being deleted
 *    carries the all-ones sentinel in its low 96 bits.
 *
 *  @note The value is `constexpr`, resolved entirely at compile time via
 *      `base_uint`'s `std::string_view` constructor, so including this header
 *      has no runtime cost and raises no ODR concerns.
 */
uint256 constexpr kPAGE_MASK(
    std::string_view("0000000000000000000000000000000000000000ffffffffffffffffffffffff"));

}  // namespace xrpl::nft
