#pragma once

#include <xrpl/basics/TaggedCache.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/STLedgerEntry.h>

namespace xrpl {
using CachedSLEs = TaggedCache<UInt256, SLE const>;
}  // namespace xrpl
