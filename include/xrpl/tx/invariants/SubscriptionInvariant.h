#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <memory>
#include <vector>

namespace xrpl {

/**
 * @brief Invariant: a Subscription ledger entry holds a well-formed balance and
 * distinct parties.
 *
 * Enforces XLS-78 2.1.1.7 for every Subscription entry the transaction leaves in
 * the ledger: Balance is not negative, Balance and Amount are denominated in the
 * same asset, and Account differs from Destination.
 */
class ValidSubscription
{
    std::vector<std::shared_ptr<STLedgerEntry const>> subscriptions_;

public:
    void
    visitEntry(bool, SLE::const_ref, SLE::const_ref);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
