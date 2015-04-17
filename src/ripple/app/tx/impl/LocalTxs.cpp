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

#include <BeastConfig.h>
#include <ripple/app/tx/LocalTxs.h>
#include <ripple/app/main/Application.h>        // getApp
#include <ripple/app/misc/CanonicalTXSet.h>
#include <ripple/protocol/Indexes.h>            // getTicketIndex

// NOTE: This class may be made OBSOLETE by the TxQ.  Consider that when
// making modifications to LocalTxs.

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

    LocalTx (LedgerIndex index, STTx::ref txn)
        : txn_ (txn)
        , expire_ (index + holdLedgers)
        , id_ (txn->getTransactionID ())
        , accountID_ (txn->getAccountID(sfAccount))
        , seq_ (txn->getSequence())
        , ticketOwnerID_ (0)
        , ticketSeq_ (0)
        , ticketIndex_ ()
    {
        if (txn->isFieldPresent (sfTicketID))
        {
            auto const& ticketID = txn->getFieldObject (sfTicketID);
            ticketOwnerID_ = ticketID.getAccountID (sfAccount);
            ticketSeq_ = ticketID.getFieldU32 (sfSequence);
            ticketIndex_ = ripple::getTicketIndex (ticketOwnerID_, ticketSeq_);
        }

        if (txn->isFieldPresent (sfLastLedgerSequence))
            expire_ = std::min (
                expire_, txn->getFieldU32 (sfLastLedgerSequence) + 1);
    }

    uint256 const& getID () const
    {
        return id_;
    }

    std::uint32_t getSeq () const
    {
        return seq_;
    }

    bool isExpired (LedgerIndex i) const
    {
        return i > expire_;
    }

    STTx::ref getTX () const
    {
        return txn_;
    }

    AccountID const& getAccountID () const
    {
        return accountID_;
    }

    bool hasTicket () const
    {
        return ticketSeq_ != 0;
    }

    uint256 const& getTicketIndex () const
    {
        return ticketIndex_;
    }

    AccountID const& getTicketOwnerID () const
    {
        return ticketOwnerID_;
    }

    std::uint32_t getTicketSeq () const
    {
        return ticketSeq_;
    }

private:

    STTx::pointer txn_;
    LedgerIndex   expire_;
    uint256       id_;
    AccountID     accountID_;
    std::uint32_t seq_;
    AccountID     ticketOwnerID_;
    std::uint32_t ticketSeq_;
    uint256       ticketIndex_;
};

//------------------------------------------------------------------------------

class LocalTxsImp : public LocalTxs
{
public:

    LocalTxsImp()
    { }

    // Add a new transaction to the set of local transactions
    void push_back (LedgerIndex index, STTx::ref txn) override
    {
        std::lock_guard <std::mutex> lock (lock_);

        txns_.emplace_back (index, txn);
    }

    bool can_remove (LocalTx& txn, Ledger::ref ledger)
    {
        // If the transaction has hung around for too many ledgers remove it.
        if (txn.isExpired (ledger->getLedgerSeq ()))
            return true;

        // If the transaction is already in the ledger remove it.
        if (hasTransaction (*ledger, txn.getID ()))
            return true;

        auto const sleAccount = cachedRead(*ledger,
            keylet::account(txn.getAccountID()).key,
                getApp().getSLECache(), ltACCOUNT_ROOT);

        // If the account that owns the transaction is not yet in the ledger,
        // keep the transaction.  The account may be funded shortly.
        if (! sleAccount)
            return false;

        // Handling changes depending on whether or not we're using Tickets.
        auto const txnSeq = txn.getSeq ();
        if ((txnSeq == 0) && (txn.hasTicket ()))
        {
            // If the Ticket is in the Ledger keep the transaction.
            if (ledger->read (keylet::ticket (txn.getTicketIndex())))
                return false;

            // If TicketOwner is missing from the ledger remove the transaction.
            auto const sleOwner = cachedRead (*ledger,
                getAccountRootIndex(txn.getTicketOwnerID()),
                    getApp().getSLECache());
            if (! sleOwner)
                return true;

            // If the Owner's sequence is greater than the Ticket's sequence
            // then the ticket either has been consumed or never existed.
            // Remove the transaction.
            if (sleOwner->getFieldU32(sfSequence) > txn.getTicketSeq())
                return true;
        }
        else
        {
            // No Ticket.  If the transaction's sequence is passed remove this.
            if (sleAccount->getFieldU32 (sfSequence) > txnSeq)
                return true;
        }
        return false;
    }

    void apply (TransactionEngine& engine) override
    {
        CanonicalTXSet tset (uint256 {});

        // Get the set of local transactions as a canonical
        // set (so they apply in a valid order)
        {
            std::lock_guard <std::mutex> lock (lock_);
            for (auto const& localTx : txns_)
                tset.push_back (localTx.getTX());
        }

        for (auto it : tset)
        {
            try
            {
                engine.applyTransaction (*it.second, tapOPEN_LEDGER);
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

    // Remove transactions that have either been accepted into a
    // fully-validated ledger, are (now) impossible, or have expired.
    void sweep (Ledger::ref validLedger) override
    {
        std::lock_guard <std::mutex> lock (lock_);

        for (auto it = txns_.begin (); it != txns_.end (); )
        {
            if (can_remove (*it, validLedger))
                it = txns_.erase (it);
            else
                ++it;
        }
    }

    std::size_t size () override
    {
        std::lock_guard <std::mutex> lock (lock_);

        return txns_.size ();
    }

private:

    std::mutex lock_;
    std::list <LocalTx> txns_;
};

std::unique_ptr <LocalTxs> LocalTxs::New()
{
    return std::make_unique <LocalTxsImp> ();
}

} // ripple
