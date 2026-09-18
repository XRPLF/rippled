#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <map>
#include <vector>

namespace xrpl {

/**
 * @brief Invariant: pseudo-accounts own only the object types their kind
 * expects.
 *
 * Each pseudo-account kind has a fixed set of ledger entry types that may
 * appear in its owner directory:
 * - AMM: the AMM entry, trust lines (IOU pool assets and LP tokens), and
 *   MPToken holdings (MPT pool assets),
 * - Vault: the share MPTokenIssuance, the asset holding (MPToken or trust
 *   line), and the LoanBrokers operating on the vault,
 * - LoanBroker: its Loans and the cover holding (MPToken or trust line).
 *
 * A pseudo-account cannot sign transactions, so an unexpected object linked
 * into its owner directory could never be accepted or removed by it. It would
 * pin the directory and block deletion of the owning object. The transactors
 * refuse to create such objects; this invariant is the backstop if any of
 * those guards is bypassed.
 */
class ValidPseudoAccountOwnership
{
    // Entries newly linked into owner directories, keyed by the directory
    // owner. Whether an owner is a pseudo-account is decided in finalize,
    // where the view is available.
    std::map<AccountID, std::vector<uint256>> added_;

public:
    void
    visitEntry(bool, SLE::const_ref, SLE::const_ref);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
