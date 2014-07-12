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

namespace ripple {

OrderBookDB::OrderBookDB (Stoppable& parent)
    : Stoppable ("OrderBookDB", parent)
    , mSeq (0)
{
}

void OrderBookDB::invalidate ()
{
    ScopedLockType sl (mLock);
    mSeq = 0;
}

void OrderBookDB::setup (Ledger::ref ledger)
{
    auto seq = ledger->getLedgerSeq ();
    {
        ScopedLockType sl (mLock);

        // Do a full update every 256 ledgers
        if (mSeq != 0)
        {
            if (seq == mSeq)
                return;
            if ((seq > mSeq) && ((seq - mSeq) < 256))
                return;
            if ((seq < mSeq) && ((mSeq - seq) < 16))
                return;
        }

        WriteLog (lsDEBUG, OrderBookDB)
            << "Advancing from " << mSeq << " to " << seq;

        mSeq = seq;
    }

    if (getConfig().RUN_STANDALONE)
        update(ledger);
    else
        getApp().getJobQueue().addJob(jtUPDATE_PF, "OrderBookDB::update",
            std::bind(&OrderBookDB::update, this, ledger));
}

static void updateHelper (SLE::ref entry,
    ripple::unordered_set< uint256 >& seen,
    OrderBookDB::IssueToOrderBook& destMap,
    OrderBookDB::IssueToOrderBook& sourceMap,
    ripple::unordered_set< Issue >& XRPBooks,
    int& books)
{
    if (entry->getType () == ltDIR_NODE &&
        entry->isFieldPresent (sfExchangeRate) &&
        entry->getFieldH256 (sfRootIndex) == entry->getIndex())
    {
        Book book;
        book.in.currency.copyFrom (entry->getFieldH160 (sfTakerPaysCurrency));
        book.in.account.copyFrom (entry->getFieldH160 (sfTakerPaysIssuer));
        book.out.account.copyFrom (entry->getFieldH160 (sfTakerGetsIssuer));
        book.out.currency.copyFrom (entry->getFieldH160 (sfTakerGetsCurrency));

        uint256 index = Ledger::getBookBase (book);
        if (seen.insert (index).second)
        {
            auto orderBook = std::make_shared<OrderBook> (index, book);
            sourceMap[book.in].push_back (orderBook);
            destMap[book.out].push_back (orderBook);
            if (isXRP(book.out))
                XRPBooks.insert(book.in);
            ++books;
        }
    }
}

void OrderBookDB::update (Ledger::pointer ledger)
{
    ripple::unordered_set< uint256 > seen;
    OrderBookDB::IssueToOrderBook destMap;
    OrderBookDB::IssueToOrderBook sourceMap;
    ripple::unordered_set< Issue > XRPBooks;

    WriteLog (lsDEBUG, OrderBookDB) << "OrderBookDB::update>";

    // walk through the entire ledger looking for orderbook entries
    int books = 0;

    try
    {
        ledger->visitStateItems(std::bind(&updateHelper, std::placeholders::_1,
                                          std::ref(seen), std::ref(destMap),
            std::ref(sourceMap), std::ref(XRPBooks), std::ref(books)));
    }
    catch (const SHAMapMissingNode&)
    {
        WriteLog (lsINFO, OrderBookDB)
            << "OrderBookDB::update encountered a missing node";
        ScopedLockType sl (mLock);
        mSeq = 0;
        return;
    }

    WriteLog (lsDEBUG, OrderBookDB)
        << "OrderBookDB::update< " << books << " books found";
    {
        ScopedLockType sl (mLock);

        mXRPBooks.swap(XRPBooks);
        mSourceMap.swap(sourceMap);
        mDestMap.swap(destMap);
    }
    getApp().getLedgerMaster().newOrderBookDB();
}

void OrderBookDB::addOrderBook(Book const& book)
{
    bool toXRP = isXRP (book.out);
    ScopedLockType sl (mLock);

    if (toXRP)
    {
        // We don't want to search through all the to-XRP or from-XRP order
        // books!
        for (auto ob: mSourceMap[book.in])
        {
            if (isXRP (ob->getCurrencyOut ())) // also to XRP
                return;
        }
    }
    else
    {
        for (auto ob: mDestMap[book.out])
        {
            if (ob->getCurrencyIn() == book.in.currency &&
                ob->getIssuerIn() == book.in.account)
            {
                return;
            }
        }
    }
    uint256 index = Ledger::getBookBase(book);
    auto orderBook = std::make_shared<OrderBook> (index, book);

    mSourceMap[book.in].push_back (orderBook);
    mDestMap[book.out].push_back (orderBook);
    if (toXRP)
        mXRPBooks.insert(book.in);
}

// return list of all orderbooks that want this issuerID and currencyID
void OrderBookDB::getBooksByTakerPays (
    Issue const& issue, OrderBook::List& bookRet)
{
    ScopedLockType sl (mLock);
    auto it = mSourceMap.find (issue);
    if (it != mSourceMap.end ())
        bookRet = it->second;
    else
        bookRet.clear ();
}

bool OrderBookDB::isBookToXRP(Issue const& issue)
{
    ScopedLockType sl (mLock);

    return mXRPBooks.count(issue) > 0;
}

// return list of all orderbooks that give this issuerID and currencyID
void OrderBookDB::getBooksByTakerGets (
    Issue const& issue, OrderBook::List& bookRet)
{
    ScopedLockType sl (mLock);
    auto it = mDestMap.find (issue);

    if (it != mDestMap.end ())
        bookRet = it->second;
    else
        bookRet.clear ();
}

BookListeners::pointer OrderBookDB::makeBookListeners (Book const& book)
{
    ScopedLockType sl (mLock);
    auto ret = getBookListeners (book);

    if (!ret)
    {
        ret = std::make_shared<BookListeners> ();

        mListeners [book] = ret;
        assert (getBookListeners (book) == ret);
    }

    return ret;
}

BookListeners::pointer OrderBookDB::getBookListeners (Book const& book)
{
    BookListeners::pointer ret;
    ScopedLockType sl (mLock);

    auto it0 = mListeners.find (book);
    if (it0 != mListeners.end ())
        ret = it0->second;

    return ret;
}

// Based on the meta, send the meta to the streams that are listening.
// We need to determine which streams a given meta effects.
void OrderBookDB::processTxn (
    Ledger::ref ledger, const AcceptedLedgerTx& alTx, Json::Value const& jvObj)
{
    ScopedLockType sl (mLock);

    if (alTx.getResult () == tesSUCCESS)
    {
        // Check if this is an offer or an offer cancel or a payment that
        // consumes an offer.
        // Check to see what the meta looks like.
        for (auto& node : alTx.getMeta ()->getNodes ())
        {
            try
            {
                if (node.getFieldU16 (sfLedgerEntryType) == ltOFFER)
                {
                    SField* field = nullptr;

                    // We need a field that contains the TakerGets and TakerPays
                    // parameters.
                    if (node.getFName () == sfModifiedNode)
                        field = &sfPreviousFields;
                    else if (node.getFName () == sfCreatedNode)
                        field = &sfNewFields;
                    else if (node.getFName () == sfDeletedNode)
                        field = &sfFinalFields;

                    if (field)
                    {
                        auto data = dynamic_cast<const STObject*> (
                            node.peekAtPField (*field));

                        if (data)
                        {
                            // determine the OrderBook
                            auto listeners = getBookListeners (
                                {data->getFieldAmount (sfTakerGets).issue(),
                                 data->getFieldAmount (sfTakerPays).issue()});

                            if (listeners)
                                listeners->publish (jvObj);
                        }
                    }
                }
            }
            catch (...)
            {
                WriteLog (lsINFO, OrderBookDB)
                    << "Fields not found in OrderBookDB::processTxn";
            }
        }
    }
}

} // ripple
