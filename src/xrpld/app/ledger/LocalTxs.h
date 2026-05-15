#pragma once

#include <xrpl/ledger/CanonicalTXSet.h>
#include <xrpl/ledger/ReadView.h>

#include <memory>

namespace xrpl {

// Track transactions issued by local clients
// Ensure we always apply them to our open ledger
// Hold them until we see them in a fully-validated ledger

class LocalTxs
{
public:
    // The number of ledgers to hold a transaction is essentially
    // arbitrary. It should be sufficient to allow the transaction to
    // get into a fully-validated ledger.
    static constexpr int kHoldLedgers = 5;

    virtual ~LocalTxs() = default;

    // Add a new local transaction
    virtual void
    pushBack(LedgerIndex index, std::shared_ptr<STTx const> const& txn) = 0;

    // Return the set of local transactions to a new open ledger
    virtual CanonicalTXSet
    getTxSet() = 0;

    // Remove obsolete transactions based on a new fully-valid ledger
    virtual void
    sweep(ReadView const& view) = 0;

    virtual std::size_t
    size() = 0;
};

std::unique_ptr<LocalTxs>
makeLocalTxs();

}  // namespace xrpl
