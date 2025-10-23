#ifndef XRPL_TEST_CSF_PROPOSAL_H_INCLUDED
#define XRPL_TEST_CSF_PROPOSAL_H_INCLUDED

#include <test/csf/Tx.h>
#include <test/csf/Validation.h>
#include <test/csf/ledgers.h>

#include <xrpld/consensus/ConsensusProposal.h>

namespace ripple {
namespace test {
namespace csf {
/** Proposal is a position taken in the consensus process and is represented
    directly from the generic types.
*/
using Proposal = ConsensusProposal<PeerID, Ledger::ID, TxSet::ID>;

}  // namespace csf
}  // namespace test
}  // namespace ripple

#endif
