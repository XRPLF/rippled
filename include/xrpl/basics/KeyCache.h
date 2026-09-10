#pragma once

#include <xrpl/basics/TaggedCache.h>
#include <xrpl/basics/base_uint.h>

namespace xrpl {

using KeyCache = TaggedCache<UInt256, int, true>;

}  // namespace xrpl
