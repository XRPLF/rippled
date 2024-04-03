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

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/OrderBookDB.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/AMMUtils.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/basics/Log.h>
#include <ripple/core/Config.h>
#include <ripple/core/JobQueue.h>
#include <ripple/protocol/Indexes.h>

namespace ripple {

OrderBookDB::OrderBookDB(Application& app)
    : app_(app), seq_(0), j_(app.journal("OrderBookDB"))
{
}

void
OrderBookDB::setup(std::shared_ptr<ReadView const> const& ledger)
{
    if (!app_.config().standalone() && app_.getOPs().isNeedNetworkLedger())
    {
        JLOG(j_.warn()) << "Eliding full order book update: no ledger";
        return;
    }

    auto seq = seq_.load();

    if (seq != 0)
    {
        if ((ledger->seq() > seq) && ((ledger->seq() - seq) < 25600))
            return;

        if ((ledger->seq() <= seq) && ((seq - ledger->seq()) < 16))
            return;
    }

    if (seq_.exchange(ledger->seq()) != seq)
        return;

    JLOG(j_.debug()) << "Full order book update: " << seq << " to "
                     << ledger->seq();

    if (app_.config().PATH_SEARCH_MAX != 0)
    {
        if (app_.config().standalone())
            update(ledger);
        else
            app_.getJobQueue().addJob(
                jtUPDATE_PF,
                "OrderBookDB::update: " + std::to_string(ledger->seq()),
                [this, ledger]() { update(ledger); });
    }
}

void
OrderBookDB::update(std::shared_ptr<ReadView const> const& ledger)
{
    if (app_.config().PATH_SEARCH_MAX == 0)
        return;  // pathfinding has been disabled

    // A newer full update job is pending
    if (auto const seq = seq_.load(); seq > ledger->seq())
    {
        JLOG(j_.debug()) << "Eliding update for " << ledger->seq()
                         << " because of pending update to later " << seq;
        return;
    }

    decltype(allBooks_) allBooks;
    decltype(xrpBooks_) xrpBooks;

    allBooks.reserve(allBooks_.size());
    xrpBooks.reserve(xrpBooks_.size());

    JLOG(j_.debug()) << "Beginning update (" << ledger->seq() << ")";

    // walk through the entire ledger looking for orderbook/AMM entries
    int cnt = 0;

    try
    {
        for (auto& sle : ledger->sles)
        {
            if (app_.isStopping())
            {
                JLOG(j_.info())
                    << "Update halted because the process is stopping";
                seq_.store(0);
                return;
            }

            if (sle->getType() == ltDIR_NODE &&
                sle->isFieldPresent(sfExchangeRate) &&
                sle->getFieldH256(sfRootIndex) == sle->key())
            {
                Book book;

                book.in.currency = sle->getFieldH160(sfTakerPaysCurrency);
                book.in.account = sle->getFieldH160(sfTakerPaysIssuer);
                book.out.currency = sle->getFieldH160(sfTakerGetsCurrency);
                book.out.account = sle->getFieldH160(sfTakerGetsIssuer);

                allBooks[book.in].insert(book.out);

                if (isXRP(book.out))
                    xrpBooks.insert(book.in);

                ++cnt;
            }
            else if (sle->getType() == ltAMM)
            {
                auto const issue1 = (*sle)[sfAsset];
                auto const issue2 = (*sle)[sfAsset2];
                auto addBook = [&](Issue const& in, Issue const& out) {
                    allBooks[in].insert(out);

                    if (isXRP(out))
                        xrpBooks.insert(in);

                    ++cnt;
                };
                addBook(issue1, issue2);
                addBook(issue2, issue1);
            }
        }
    }
    catch (SHAMapMissingNode const& mn)
    {
        JLOG(j_.info()) << "Missing node in " << ledger->seq()
                        << " during update: " << mn.what();
        seq_.store(0);
        return;
    }

    JLOG(j_.debug()) << "Update completed (" << ledger->seq() << "): " << cnt
                     << " books found";

    {
        std::lock_guard sl(mLock);
        allBooks_.swap(allBooks);
        xrpBooks_.swap(xrpBooks);
    }

    app_.getLedgerMaster().newOrderBookDB();
}

void
OrderBookDB::addOrderBook(Book const& book)
{
    bool toXRP = isXRP(book.out);

    std::lock_guard sl(mLock);

    allBooks_[book.in].insert(book.out);

    if (toXRP)
        xrpBooks_.insert(book.in);
}

// return list of all orderbooks that want this issuerID and currencyID
std::vector<Book>
OrderBookDB::getBooksByTakerPays(Issue const& issue)
{
    std::vector<Book> ret;

    {
        std::lock_guard sl(mLock);

        if (auto it = allBooks_.find(issue); it != allBooks_.end())
        {
            ret.reserve(it->second.size());

            for (auto const& gets : it->second)
                ret.push_back(Book(issue, gets));
        }
    }

    return ret;
}

int
OrderBookDB::getBookSize(Issue const& issue)
{
    std::lock_guard sl(mLock);
    if (auto it = allBooks_.find(issue); it != allBooks_.end())
        return static_cast<int>(it->second.size());
    return 0;
}

bool
OrderBookDB::isBookToXRP(Issue const& issue)
{
    std::lock_guard sl(mLock);
    return xrpBooks_.count(issue) > 0;
}

BookListeners::pointer
OrderBookDB::makeBookListeners(Book const& book)
{
    std::lock_guard sl(mLock);
    auto ret = getBookListeners(book);

    if (!ret)
    {
        ret = std::make_shared<BookListeners>();

        mListeners[book] = ret;
        assert(getBookListeners(book) == ret);
    }

    return ret;
}

BookListeners::pointer
OrderBookDB::getBookListeners(Book const& book)
{
    BookListeners::pointer ret;
    std::lock_guard sl(mLock);

    auto it0 = mListeners.find(book);
    if (it0 != mListeners.end())
        ret = it0->second;

    return ret;
}

// Based on the meta, send the meta to the streams that are listening.
// We need to determine which streams a given meta effects.
void
OrderBookDB::processTxn(
    std::shared_ptr<ReadView const> const& ledger,
    const AcceptedLedgerTx& alTx,
    MultiApiJson const& jvObj)
{
    std::lock_guard sl(mLock);

    // For this particular transaction, maintain the set of unique
    // subscriptions that have already published it.  This prevents sending
    // the transaction multiple times if it touches multiple ltOFFER
    // entries for the same book, or if it touches multiple books and a
    // single client has subscribed to those books.
    hash_set<std::uint64_t> havePublished;

    for (auto const& node : alTx.getMeta().getNodes())
    {
        try
        {
            if (node.getFieldU16(sfLedgerEntryType) == ltOFFER)
            {
                auto process = [&, this](SField const& field) {
                    if (auto data = dynamic_cast<STObject const*>(
                            node.peekAtPField(field));
                        data && data->isFieldPresent(sfTakerPays) &&
                        data->isFieldPresent(sfTakerGets))
                    {
                        auto listeners = getBookListeners(
                            {data->getFieldAmount(sfTakerGets).issue(),
                             data->getFieldAmount(sfTakerPays).issue()});
                        if (listeners)
                            listeners->publish(jvObj, havePublished);
                    }
                };

                // We need a field that contains the TakerGets and TakerPays
                // parameters.
                if (node.getFName() == sfModifiedNode)
                    process(sfPreviousFields);
                else if (node.getFName() == sfCreatedNode)
                    process(sfNewFields);
                else if (node.getFName() == sfDeletedNode)
                    process(sfFinalFields);
            }
        }
        catch (std::exception const& ex)
        {
            JLOG(j_.info())
                << "processTxn: field not found (" << ex.what() << ")";
        }
    }
}

}  // namespace ripple
