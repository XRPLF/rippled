#pragma once

#include <xrpl/basics/TaggedCache.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/STLedgerEntry.h>

namespace xrpl {
using CachedSLEs = TaggedCache<uint256, SLE const>;
}
