#pragma once

#include <xrpl/basics/base_uint.h>

#include <string_view>

namespace xrpl::nft {

// NFT directory pages order their contents based only on the low 96 bits of
// the NFToken value.  This mask provides easy access to the necessary mask.
constexpr uint256 kPageMask(
    std::string_view("0000000000000000000000000000000000000000ffffffffffffffffffffffff"));

}  // namespace xrpl::nft
