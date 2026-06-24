#pragma once

#include <xrpl/protocol/STLedgerEntry.h>

#include <cstdint>

namespace xrpl {

std::int32_t
signerListOwnerCount(SLE const& sle);

}  // namespace xrpl
