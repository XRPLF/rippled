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

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/ledger/LocalTxs.h>
#include <ripple/app/main/Application.h>
#include <ripple/protocol/Indexes.h>

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

    LocalTx (LedgerIndex index, std::shared_ptr<STTx const> const& txn)
        : m_txn (txn)
        , m_expire (index + holdLedgers)
        , m_id (txn->getTransactionID ())
        , m_account (txn->getAccountID(sfAccount))
        , m_seq (txn->getSequence())
    {
        if (txn->isFieldPresent (sfLastLedgerSequence))
            m_expire = std::min (m_expire, txn->getFieldU32 (sfLastLedgerSequence) + 1);
    }

    uint256 const& getID () const
    {
        return m_id;
    }

    std::uint32_t getSeq () const
    {
        return m_seq;
    }

    bool isExpired (LedgerIndex i) const
    {
        return i > m_expire;
    }

    std::shared_ptr<STTx const> const& getTX () const
    {
        return m_txn;
    }

    AccountID const& getAccount () const
    {
        return m_account;
    }

private:

    std::shared_ptr<STTx const> m_txn;
    LedgerIndex                    m_expire;
    uint256                        m_id;
    AccountID                      m_account;
    std::uint32_t                  m_seq;
};

//------------------------------------------------------------------------------

class LocalTxsImp
    : public LocalTxs
{
public:
    LocalTxsImp() = default;

    // Add a new transaction to the set of local transactions
    void push_back (LedgerIndex index, std::shared_ptr<STTx const> const& txn) override
    {
        std::lock_guard <std::mutex> lock (m_lock);

        m_txns.emplace_back (index, txn);
    }

    CanonicalTXSet
    getTxSet () override
    {
        CanonicalTXSet tset (uint256 {});

        // Get the set of local transactions as a canonical
        // set (so they apply in a valid order)
        {
            std::lock_guard <std::mutex> lock (m_lock);

            for (auto const& it : m_txns)
                tset.insert (it.getTX());
        }

        return tset;
    }

    // Remove transactions that have either been accepted
    // into a fully-validated ledger, are (now) impossible,
    // or have expired
    void sweep (ReadView const& view) override
    {
        std::lock_guard <std::mutex> lock (m_lock);

        m_txns.remove_if ([&view](auto const& txn)
        {
            if (txn.isExpired (view.info().seq))
                return true;
            if (view.txExists(txn.getID()))
                return true;
            auto const sle = cachedRead(view,
                keylet::account(txn.getAccount()).key, ltACCOUNT_ROOT);
            if (! sle)
                return false;
            return sle->getFieldU32 (sfSequence) > txn.getSeq ();
        });
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

std::unique_ptr<LocalTxs>
make_LocalTxs ()
{
    return std::make_unique<LocalTxsImp> ();
}

} // ripple
