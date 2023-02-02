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
#include <ripple/protocol/Indexes.h>
#include <algorithm>
#include <vector>

namespace ripple {

/** Wraps a pointer to a transaction along with an expiration ledger. */
class LocalTx
{
public:
    LocalTx(LedgerIndex index, std::shared_ptr<STTx const> const& txn)
        : txn_(txn)
        , expire_(index)
        , account_(txn->getAccountID(sfAccount))
        , seqProxy_(txn->getSeqProxy())
    {
        if (txn->isFieldPresent(sfLastLedgerSequence))
            expire_ =
                std::min(expire_, txn->getFieldU32(sfLastLedgerSequence) + 1);
    }

    LocalTx(LocalTx&& other) = default;
    LocalTx&
    operator=(LocalTx&& other) = default;

    LocalTx(LocalTx const& other) = delete;
    LocalTx&
    operator=(LocalTx const& other) = delete;

    [[nodiscard]] uint256
    getID() const
    {
        return txn_->getTransactionID();
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
    AccountID account_;
    SeqProxy seqProxy_;
};

//------------------------------------------------------------------------------

class LocalTxsImp : public LocalTxs
{
    // The number of ledgers to hold a transaction is essentially
    // arbitrary. It should be sufficient to allow the transaction to
    // get into a fully-validated ledger.
    static constexpr std::uint32_t const holdLedgers = 5;

public:
    LocalTxsImp()
    {
        txns_.reserve(512);
    }

    void
    track(std::shared_ptr<STTx const> const& txn, LedgerIndex index) override
    {
        std::lock_guard lock(lock_);
        txns_.emplace_back(index + holdLedgers, txn);
    }

    [[nodiscard]] CanonicalTXSet
    getTransactions() override
    {
        CanonicalTXSet tset(uint256{});

        // Get the set of local transactions as a canonical set (so they apply
        // in a valid order)
        {
            std::lock_guard lock(lock_);

            for (auto const& it : txns_)
                tset.insert(it.getTX());
        }
        return tset;
    }

    void
    sweep(ReadView const& view) override
    {
        std::lock_guard lock(lock_);

        auto ret = std::remove_if(
            txns_.begin(), txns_.end(), [&view](auto const& txn) {
                if (txn.isExpired(view.info().seq))
                    return true;

                if (view.txExists(txn.getID()))
                    return true;

                AccountID const acctID = txn.getAccount();
                auto const sleAcct = view.read(keylet::account(acctID));

                if (!sleAcct)
                    return false;

                SeqProxy const acctSeq =
                    SeqProxy::sequence(sleAcct->getFieldU32(sfSequence));
                SeqProxy const seqProx = txn.getSeqProxy();

                if (seqProx.isSeq())
                    return acctSeq > seqProx;  // Remove tefPAST_SEQ

                // Keep ticket from the future but note that the tx will not be
                // held for more than `holdLedgers` ledgers.
                if (seqProx.isTicket() && acctSeq.value() <= seqProx.value())
                    return false;

                // Ticket should have been created by now. Remove if ticket
                // does not exist.
                return !view.exists(keylet::ticket(acctID, seqProx));
            });

        txns_.erase(ret, txns_.end());

        if (txns_.capacity() >= 65536)
            txns_.reserve(65535);
    }

    std::size_t
    size() override
    {
        std::lock_guard lock(lock_);
        return txns_.size();
    }

private:
    std::mutex lock_;
    std::vector<LocalTx> txns_;
};

std::unique_ptr<LocalTxs>
make_LocalTxs()
{
    return std::make_unique<LocalTxsImp>();
}

}  // namespace ripple
