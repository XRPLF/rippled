//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

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


namespace ripple {

// This class wraps a pointer to a transaction along with
// its expiration ledger. It also caches the issuing account.
class LocalTx
{
public:

    // The number of ledgers to hold a transaction is essentially
    // arbitrary. It should be sufficient to allow the transaction to
    // get into a fully-validated ledger.
    static int const holdLedgers = 5;

    LocalTx (LedgerIndex index, SerializedTransaction::ref txn)
        : m_txn (txn)
        , m_expire (index + holdLedgers)
        , m_id (txn->getTransactionID ())
        , m_account (txn->getSourceAccount ())
        , m_seq (txn->getSequence())
    {
        if (txn->isFieldPresent (sfLastLedgerSequence))
        {
           LedgerIndex m_txnexpire = txn->getFieldU32 (sfLastLedgerSequence) + 1;
           m_expire = std::min (m_expire, m_txnexpire);
        }
    }

    uint256 const& getID ()
    {
        return m_id;
    }

    std::uint32_t getSeq ()
    {
        return m_seq;
    }

    bool isExpired (LedgerIndex i)
    {
        return i > m_expire;
    }

    SerializedTransaction::ref getTX ()
    {
        return m_txn;
    }

    RippleAddress const& getAccount ()
    {
        return m_account;
    }

private:

    SerializedTransaction::pointer m_txn;
    LedgerIndex                    m_expire;
    uint256                        m_id;
    RippleAddress                  m_account;
    std::uint32_t                  m_seq;
};

class LocalTxsImp : public LocalTxs
{
public:

    LocalTxsImp()
    { }

    // Add a new transaction to the set of local transactions
    void push_back (LedgerIndex index, SerializedTransaction::ref txn) override
    {
        std::lock_guard <std::mutex> lock (m_lock);

        m_txns.emplace_back (index, txn);
    }

    bool can_remove (LocalTx& txn, Ledger::ref ledger)
    {

        if (txn.isExpired (ledger->getLedgerSeq ()))
            return true;

        if (ledger->hasTransaction (txn.getID ()))
            return true;

        SLE::pointer sle = ledger->getAccountRoot (txn.getAccount ());
        if (!sle)
            return false;

        if (sle->getFieldU32 (sfSequence) > txn.getSeq ())
            return true;


        return false;
    }

    void apply (TransactionEngine& engine) override
    {

        CanonicalTXSet tset (uint256 {});

        // Get the set of local transactions as a canonical
        // set (so they apply in a valid order)
        {
            std::lock_guard <std::mutex> lock (m_lock);

            for (auto& it : m_txns)
                tset.push_back (it.getTX());
        }

        for (auto it : tset)
        {
            try
            {
                TransactionEngineParams parms = tapOPEN_LEDGER;
                bool didApply;
                engine.applyTransaction (*it.second, parms, didApply);
            }
            catch (...)
            {
                // Nothing special we need to do.
                // It's possible a cleverly malformed transaction or
                // corrupt back end database could cause an exception
                // during transaction processing.
            }
        }
    }

    // Remove transactions that have either been accepted into a fully-validated
    // ledger, are (now) impossible, or have expired
    void sweep (Ledger::ref validLedger) override
    {
        std::lock_guard <std::mutex> lock (m_lock);

        for (auto it = m_txns.begin (); it != m_txns.end (); )
        {
            if (can_remove (*it, validLedger))
                it = m_txns.erase (it);
            else
                ++it;
        }
    }

    std::size_t size () override
    {
        std::lock_guard <std::mutex> lock (m_lock);

        return m_txns.size ();
    }

private:

    std::mutex m_lock;
    std::list <LocalTx> m_txns;
};

std::unique_ptr <LocalTxs> LocalTxs::New()
{
    return std::make_unique <LocalTxsImp> ();
}

} // ripple
