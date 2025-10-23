#ifndef XRPL_LEDGER_CACHEDSLES_H_INCLUDED
#define XRPL_LEDGER_CACHEDSLES_H_INCLUDED

#include <xrpl/basics/TaggedCache.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/STLedgerEntry.h>

namespace ripple {
using CachedSLEs = TaggedCache<uint256, SLE const>;
}

#endif  // XRPL_LEDGER_CACHEDSLES_H_INCLUDED
