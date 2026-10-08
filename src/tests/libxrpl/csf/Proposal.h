#pragma once

#include <xrpl/consensus/ConsensusProposal.h>

#include <csf/Tx.h>
#include <csf/Validation.h>
#include <csf/ledgers.h>

namespace xrpl::test::csf {
/**
 * Proposal is a position taken in the consensus process and is represented
 * directly from the generic types.
 */
using Proposal = ConsensusProposal<PeerID, Ledger::ID, TxSet::ID>;

}  // namespace xrpl::test::csf
