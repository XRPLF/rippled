#ifndef XRPL_APP_LEDGER_LOCALTXS_H_INCLUDED
#define XRPL_APP_LEDGER_LOCALTXS_H_INCLUDED

#include <xrpld/app/misc/CanonicalTXSet.h>

#include <xrpl/ledger/ReadView.h>

#include <memory>

namespace ripple {

// Track transactions issued by local clients
// Ensure we always apply them to our open ledger
// Hold them until we see them in a fully-validated ledger

class LocalTxs
{
public:
    // The number of ledgers to hold a transaction is essentially
    // arbitrary. It should be sufficient to allow the transaction to
    // get into a fully-validated ledger.
    static constexpr int holdLedgers = 5;

    virtual ~LocalTxs() = default;

    // Add a new local transaction
    virtual void
    push_back(LedgerIndex index, std::shared_ptr<STTx const> const& txn) = 0;

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
make_LocalTxs();

}  // namespace ripple

#endif
