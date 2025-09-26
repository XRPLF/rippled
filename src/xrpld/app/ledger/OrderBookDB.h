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

#ifndef XRPL_APP_LEDGER_ORDERBOOKDB_H_INCLUDED
#define XRPL_APP_LEDGER_ORDERBOOKDB_H_INCLUDED

#include <xrpld/app/ledger/AcceptedLedgerTx.h>
#include <xrpld/app/ledger/BookListeners.h>
#include <xrpld/app/main/Application.h>

#include <xrpl/protocol/MultiApiJson.h>
#include <xrpl/protocol/UintTypes.h>

#include <mutex>
#include <optional>

namespace ripple {

class OrderBookDB
{
public:
    explicit OrderBookDB(Application& app);

    void
    setup(std::shared_ptr<ReadView const> const& ledger);
    void
    update(std::shared_ptr<ReadView const> const& ledger);

    void
    addOrderBook(Book const&);

    /** @return a list of all orderbooks that want this issuerID and currencyID.
     */
    std::vector<Book>
    getBooksByTakerPays(
        Issue const&,
        std::optional<Domain> const& domain = std::nullopt);

    /** @return a count of all orderbooks that want this issuerID and
        currencyID. */
    int
    getBookSize(
        Issue const&,
        std::optional<Domain> const& domain = std::nullopt);

    bool
    isBookToXRP(Issue const&, std::optional<Domain> domain = std::nullopt);

    BookListeners::pointer
    getBookListeners(Book const&);
    BookListeners::pointer
    makeBookListeners(Book const&);

    // see if this txn effects any orderbook
    void
    processTxn(
        std::shared_ptr<ReadView const> const& ledger,
        AcceptedLedgerTx const& alTx,
        MultiApiJson const& jvObj);

private:
    Application& app_;

    // Maps order books by "issue in" to "issue out":
    hardened_hash_map<Issue, hardened_hash_set<Issue>> allBooks_;

    hardened_hash_map<std::pair<Issue, Domain>, hardened_hash_set<Issue>>
        domainBooks_;

    // does an order book to XRP exist
    hash_set<Issue> xrpBooks_;

    // does an order book to XRP exist
    hash_set<std::pair<Issue, Domain>> xrpDomainBooks_;

    std::recursive_mutex mLock;

    using BookToListenersMap = hash_map<Book, BookListeners::pointer>;

    BookToListenersMap mListeners;

    std::atomic<std::uint32_t> seq_;

    beast::Journal const j_;
};

}  // namespace ripple

#endif
