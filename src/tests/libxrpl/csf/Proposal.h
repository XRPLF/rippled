#pragma once

#include <tests/libxrpl/csf/Tx.h>
#include <tests/libxrpl/csf/Validation.h>
#include <tests/libxrpl/csf/ledgers.h>

#include <xrpl/consensus/ConsensusProposal.h>

namespace xrpl::test::csf {
/**
 * Proposal is a position taken in the consensus process and is represented
 * directly from the generic types.
 */
using Proposal = ConsensusProposal<PeerID, Ledger::ID, TxSet::ID>;

}  // namespace xrpl::test::csf
