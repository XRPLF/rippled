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
        : m_txn(txn)
        , m_expire(index + LocalTxs::holdLedgers)
        , m_id(txn->getTransactionID())
        , m_account(txn->getAccountID(sfAccount))
        , m_seqProxy(txn->getSeqProxy())
    {
        if (txn->isFieldPresent(sfLastLedgerSequence))
            m_expire = std::min(m_expire, txn->getFieldU32(sfLastLedgerSequence) + 1);
    }

    [[nodiscard]] uint256 const&
    getID() const
    {
        return m_id;
    }

    [[nodiscard]] SeqProxy
    getSeqProxy() const
    {
        return m_seqProxy;
    }

    [[nodiscard]] bool
    isExpired(LedgerIndex i) const
    {
        return i > m_expire;
    }

    [[nodiscard]] std::shared_ptr<STTx const> const&
    getTX() const
    {
        return m_txn;
    }

    [[nodiscard]] AccountID const&
    getAccount() const
    {
        return m_account;
    }

private:
    std::shared_ptr<STTx const> m_txn;
    LedgerIndex m_expire;
    uint256 m_id;
    AccountID m_account;
    SeqProxy m_seqProxy;
};

//------------------------------------------------------------------------------

class LocalTxsImp : public LocalTxs
{
public:
    LocalTxsImp() = default;

    // Add a new transaction to the set of local transactions
    void
    push_back(LedgerIndex index, std::shared_ptr<STTx const> const& txn) override
    {
        std::scoped_lock const lock(m_lock);

        m_txns.emplace_back(index, txn);
    }

    CanonicalTXSet
    getTxSet() override
    {
        CanonicalTXSet tset(uint256{});

        // Get the set of local transactions as a canonical
        // set (so they apply in a valid order)
        {
            std::scoped_lock const lock(m_lock);

            for (auto const& it : m_txns)
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
        std::scoped_lock const lock(m_lock);

        m_txns.remove_if([&view](auto const& txn) {
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
            return !view.exists(keylet::ticket(acctID, seqProx));
        });
    }

    std::size_t
    size() override
    {
        std::scoped_lock const lock(m_lock);

        return m_txns.size();
    }

private:
    std::mutex m_lock;
    std::list<LocalTx> m_txns;
};

std::unique_ptr<LocalTxs>
make_LocalTxs()
{
    return std::make_unique<LocalTxsImp>();
}

}  // namespace xrpl
