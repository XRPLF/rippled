#ifndef XRPL_PROTOCOL_NFT_PAGE_MASK_H_INCLUDED
#define XRPL_PROTOCOL_NFT_PAGE_MASK_H_INCLUDED

#include <xrpl/basics/base_uint.h>

#include <string_view>

namespace ripple {
namespace nft {

// NFT directory pages order their contents based only on the low 96 bits of
// the NFToken value.  This mask provides easy access to the necessary mask.
uint256 constexpr pageMask(std::string_view(
    "0000000000000000000000000000000000000000ffffffffffffffffffffffff"));

}  // namespace nft
}  // namespace ripple

#endif
