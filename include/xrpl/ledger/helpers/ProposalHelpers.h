#pragma once

#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TxFormats.h>

#include <cstdint>

namespace xrpl::proposal {

/**
 * Owner-reserve increments held by a proposal of an ordinary transaction.
 */
constexpr std::uint32_t kProposalOwnerCount = 5;

/**
 * Owner-reserve increments held by a proposal of a Batch transaction. A
 * proposed Batch stores up to eight inner transactions plus multi-account
 * signatures, so it reserves more than an ordinary proposed transaction.
 */
constexpr std::uint32_t kBatchProposalOwnerCount = 10;

/**
 * Owner-reserve increments held by a proposal of the given transaction.
 *
 * Lives in the ledger module (rather than alongside the rest of
 * ProposalHelpers.h in the tx module) so that both TransactionProposalCreate
 * (tx module) and SponsorHelpers' getLedgerEntryOwnerCount (ledger module)
 * can share it without either module depending on the other in the wrong
 * direction.
 */
inline std::uint32_t
proposalOwnerCount(STObject const& proposedTx)
{
    return proposedTx.getFieldU16(sfTransactionType) == ttBATCH ? kBatchProposalOwnerCount
                                                                : kProposalOwnerCount;
}

}  // namespace xrpl::proposal
