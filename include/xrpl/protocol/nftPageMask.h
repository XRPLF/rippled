#pragma once

#include <xrpl/basics/base_uint.h>

#include <string_view>

namespace xrpl {
namespace nft {

// NFT directory pages order their contents based only on the low 96 bits of
// the NFToken value.  This mask provides easy access to the necessary mask.
uint256 constexpr pageMask(std::string_view("0000000000000000000000000000000000000000ffffffffffffffffffffffff"));

}  // namespace nft
}  // namespace xrpl
