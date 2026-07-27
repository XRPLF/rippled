#pragma once

#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TxFormats.h>

#include <cstdint>

namespace xrpl {

/**
 * Owner-reserve increments held by a proposal, as defined by the
 * On-Chain Cosigner XLS section 4.4.
 */
inline constexpr std::uint32_t ordinaryProposalOwnerCount = 5;
inline constexpr std::uint32_t batchProposalOwnerCount = 10;

inline std::uint32_t
proposalOwnerCount(STObject const& proposedTx)
{
    return proposedTx.getFieldU16(sfTransactionType) == ttBATCH ? batchProposalOwnerCount
                                                                : ordinaryProposalOwnerCount;
}

}  // namespace xrpl
