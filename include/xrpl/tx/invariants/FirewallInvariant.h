#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <utility>
#include <vector>

namespace xrpl {

/**
 * A firewall keeps its owner and its counterparty.
 *
 * Every Firewall entry names a counterparty, because without one no update or
 * deletion could ever be authorized and the account's value would be stranded.
 * The owner is fixed at creation, so a firewall cannot be moved to another
 * account, and the counterparty is never the owner, which would let the owner
 * authorize its own updates.
 */
class ValidFirewall
{
    // <before, after>. before is unseated when the entry is being created.
    std::vector<std::pair<SLE::const_pointer, SLE::const_pointer>> firewalls_;

public:
    void
    visitEntry(bool, SLE::const_ref, SLE::const_ref);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
