#include <xrpld/app/ledger/LocalTxs.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/ledger/CanonicalTXSet.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STTx.h>

#include <algorithm>
#include <cstddef>
#include <list>
#include <memory>
#include <mutex>

/*
 This code prevents scenarios like the following:
1) A client submits a transaction.
2) The transaction gets into the ledger this server
   believes will be the consensus ledger.
3) The server builds a succeeding open ledger without the
   transaction (because it's in the prior ledger).
4) The local consensus ledger is not the majority ledger
   (due to network conditions, Byzantine fault, etcetera)
   the majority ledger does not include the transaction.
5) The server builds a new open ledger that does not include
   the transaction or have it in a prior ledger.
6) The client submits another transaction and gets a terPRE_SEQ
   preliminary result.
7) The server does not relay that second transaction, at least
   not yet.

With this code, when step 5 happens, the first transaction will
be applied to that open ledger so the second transaction will
succeed normally at step 6. Transactions remain tracked and
test-applied to all new open ledgers until seen in a fully-
validated ledger
*/

namespace xrpl {

// This class wraps a pointer to a transaction along with
// its expiration ledger. It also caches the issuing account.
class LocalTx
{
public:
    LocalTx(LedgerIndex index, std::shared_ptr<STTx const> const& txn)
        : txn_(txn)
        , expire_(index + LocalTxs::kHoldLedgers)
        , id_(txn->getTransactionID())
        , account_(txn->getAccountID(sfAccount))
        , seqProxy_(txn->getSeqProxy())
    {
        if (txn->isFieldPresent(sfLastLedgerSequence))
            expire_ = std::min(expire_, txn->getFieldU32(sfLastLedgerSequence) + 1);
    }

    [[nodiscard]] uint256 const&
    getID() const
    {
        return id_;
    }

    [[nodiscard]] SeqProxy
    getSeqProxy() const
    {
        return seqProxy_;
    }

    [[nodiscard]] bool
    isExpired(LedgerIndex i) const
    {
        return i > expire_;
    }

    [[nodiscard]] std::shared_ptr<STTx const> const&
    getTX() const
    {
        return txn_;
    }

    [[nodiscard]] AccountID const&
    getAccount() const
    {
        return account_;
    }

private:
    std::shared_ptr<STTx const> txn_;
    LedgerIndex expire_;
    uint256 id_;
    AccountID account_;
    SeqProxy seqProxy_;
};

//------------------------------------------------------------------------------

class LocalTxsImp : public LocalTxs
{
public:
    LocalTxsImp() = default;

    // Add a new transaction to the set of local transactions
    void
    pushBack(LedgerIndex index, std::shared_ptr<STTx const> const& txn) override
    {
        std::scoped_lock const lock(lock_);

        txns_.emplace_back(index, txn);
    }

    CanonicalTXSet
    getTxSet() override
    {
        CanonicalTXSet tset(uint256{});

        // Get the set of local transactions as a canonical
        // set (so they apply in a valid order)
        {
            std::scoped_lock const lock(lock_);

            for (auto const& it : txns_)
                tset.insert(it.getTX());
        }
        return tset;
    }

    // Remove transactions that have either been accepted
    // into a fully-validated ledger, are (now) impossible,
    // or have expired
    void
    sweep(ReadView const& view) override
    {
        std::scoped_lock const lock(lock_);

        txns_.remove_if([&view](auto const& txn) {
            if (txn.isExpired(view.header().seq))
                return true;
            if (view.txExists(txn.getID()))
                return true;

            AccountID const acctID = txn.getAccount();
            auto const sleAcct = view.read(keylet::account(acctID));

            if (!sleAcct)
                return false;

            SeqProxy const acctSeq = SeqProxy::sequence(sleAcct->getFieldU32(sfSequence));
            SeqProxy const seqProx = txn.getSeqProxy();

            if (seqProx.isSeq())
                return acctSeq > seqProx;  // Remove tefPAST_SEQ

            if (seqProx.isTicket() && acctSeq.value() <= seqProx.value())
            {
                // Keep ticket from the future.  Note, however, that the
                // transaction will not be held indefinitely since LocalTxs
                // will only hold a transaction for a maximum of 5 ledgers.
                return false;
            }

            // Ticket should have been created by now.  Remove if ticket
            // does not exist.
            return !view.exists(keylet::kTicket(acctID, seqProx));
        });
    }

    std::size_t
    size() override
    {
        std::scoped_lock const lock(lock_);

        return txns_.size();
    }

private:
    std::mutex lock_;
    std::list<LocalTx> txns_;
};

std::unique_ptr<LocalTxs>
makeLocalTxs()
{
    return std::make_unique<LocalTxsImp>();
}

}  // namespace xrpl
