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

#ifndef RIPPLE_APP_LEDGER_ORDERBOOKDB_H_INCLUDED
#define RIPPLE_APP_LEDGER_ORDERBOOKDB_H_INCLUDED

#include <ripple/app/ledger/AcceptedLedgerTx.h>
#include <ripple/app/ledger/BookListeners.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/OrderBook.h>
#include <mutex>

namespace ripple {

class OrderBookDB
{
public:
    OrderBookDB(Application& app);

    void
    setup(std::shared_ptr<ReadView const> const& ledger);
    void
    update(std::shared_ptr<ReadView const> const& ledger);
    void
    invalidate();

    void
    addOrderBook(Book const&);

    /** @return a list of all orderbooks that want this issuerID and currencyID.
     */
    OrderBook::List
    getBooksByTakerPays(Issue const&);

    /** @return a count of all orderbooks that want this issuerID and
        currencyID. */
    int
    getBookSize(Issue const&);

    bool
    isBookToXRP(Issue const&);

    BookListeners::pointer
    getBookListeners(Book const&);
    BookListeners::pointer
    makeBookListeners(Book const&);

    // see if this txn effects any orderbook
    void
    processTxn(
        std::shared_ptr<ReadView const> const& ledger,
        const AcceptedLedgerTx& alTx,
        Json::Value const& jvObj);

    using IssueToOrderBook = hash_map<Issue, OrderBook::List>;

private:
    void
    rawAddBook(Book const&);

    Application& app_;

    // by ci/ii
    IssueToOrderBook mSourceMap;

    // by co/io
    IssueToOrderBook mDestMap;

    // does an order book to XRP exist
    hash_set<Issue> mXRPBooks;

    std::recursive_mutex mLock;

    using BookToListenersMap = hash_map<Book, BookListeners::pointer>;

    BookToListenersMap mListeners;

    std::uint32_t mSeq;

    beast::Journal const j_;
};

}  // namespace ripple

#endif
