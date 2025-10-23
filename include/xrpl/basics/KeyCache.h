#ifndef XRPL_BASICS_KEYCACHE_H
#define XRPL_BASICS_KEYCACHE_H

#include <xrpl/basics/TaggedCache.h>
#include <xrpl/basics/base_uint.h>

namespace ripple {

using KeyCache = TaggedCache<uint256, int, true>;

}  // namespace ripple

#endif  // XRPL_BASICS_KEYCACHE_H
