#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <memory>
#include <utility>
#include <vector>

namespace xrpl {

/**
 * @brief Invariant: a Subscription ledger entry holds a well-formed balance and
 * distinct parties.
 *
 * Enforces XLS-78 2.1.1.7 for every Subscription entry the transaction leaves in
 * the ledger: Balance is not negative, Balance and Amount are denominated in the
 * same asset, and Account differs from Destination. A SubscriptionClaim never
 * moves NextClaimTime backwards.
 */
class ValidSubscription
{
    // Each modified or created Subscription entry: its state before (null when created) and after.
    std::vector<
        std::pair<std::shared_ptr<STLedgerEntry const>, std::shared_ptr<STLedgerEntry const>>>
        subscriptions_;

public:
    void
    visitEntry(bool, SLE::ConstRef, SLE::ConstRef);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
