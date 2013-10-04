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


SETUP_LOG (OrderBookDB)

OrderBookDB::OrderBookDB (Stoppable& parent)
    : Stoppable ("OrderBookDB", parent)
    , mLock (this, "OrderBookDB", __FILE__, __LINE__)
    , mSeq (0)
{

}

void OrderBookDB::invalidate ()
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    mSeq = 0;
}

void OrderBookDB::setup (Ledger::ref ledger)
{
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);

        // Do a full update every 256 ledgers
        if (mSeq != 0)
        {
            if (ledger->getLedgerSeq () == mSeq)
                return;
            if ((ledger->getLedgerSeq () > mSeq) && ((ledger->getLedgerSeq () - mSeq) < 256))
                return;
            if ((ledger->getLedgerSeq () < mSeq) && ((mSeq - ledger->getLedgerSeq ()) < 16))
                return;
        }

        WriteLog (lsDEBUG, OrderBookDB) << "Advancing from " << mSeq << " to " << ledger->getLedgerSeq();

        mSeq = ledger->getLedgerSeq ();
    }

    if (getConfig().RUN_STANDALONE)
        update(ledger);
    else
        getApp().getJobQueue().addJob(jtUPDATE_PF, "OrderBookDB::update",
            BIND_TYPE(&OrderBookDB::update, this, ledger));
}

void OrderBookDB::update (Ledger::pointer ledger)
{
    LoadEvent::autoptr ev = getApp().getJobQueue ().getLoadEventAP (jtOB_SETUP, "OrderBookDB::update");

    boost::unordered_set< uint256 > seen;
    boost::unordered_map< currencyIssuer_t, std::vector<OrderBook::pointer> > destMap;
    boost::unordered_map< currencyIssuer_t, std::vector<OrderBook::pointer> > sourceMap;
    boost::unordered_set< currencyIssuer_t > XRPBooks;

    WriteLog (lsDEBUG, OrderBookDB) << "OrderBookDB::update>";

    // walk through the entire ledger looking for orderbook entries
    int books = 0;
    uint256 currentIndex = ledger->getFirstLedgerIndex ();

    while (currentIndex.isNonZero ())
    {
        SLE::pointer entry = ledger->getSLEi (currentIndex);

        if ((entry->getType () == ltDIR_NODE) && (entry->isFieldPresent (sfExchangeRate)) &&
                (entry->getFieldH256 (sfRootIndex) == currentIndex))
        {
            const uint160& ci = entry->getFieldH160 (sfTakerPaysCurrency);
            const uint160& co = entry->getFieldH160 (sfTakerGetsCurrency);
            const uint160& ii = entry->getFieldH160 (sfTakerPaysIssuer);
            const uint160& io = entry->getFieldH160 (sfTakerGetsIssuer);

            uint256 index = Ledger::getBookBase (ci, ii, co, io);

            if (seen.insert (index).second)
            {
                // VFALCO TODO Reduce the clunkiness of these parameter wrappers
                OrderBook::pointer book = boost::make_shared<OrderBook> (boost::cref (index),
                                          boost::cref (ci), boost::cref (co), boost::cref (ii), boost::cref (io));

                sourceMap[currencyIssuer_ct (ci, ii)].push_back (book);
                destMap[currencyIssuer_ct (co, io)].push_back (book);
                if (co.isZero())
                    XRPBooks.insert(currencyIssuer_ct (ci, ii));
                ++books;
            }
        }

        currentIndex = ledger->getNextLedgerIndex (currentIndex);
    }

    WriteLog (lsDEBUG, OrderBookDB) << "OrderBookDB::update< " << books << " books found";
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);

        mXRPBooks.swap(XRPBooks);
        mSourceMap.swap(sourceMap);
        mDestMap.swap(destMap);
    }
}

void OrderBookDB::addOrderBook(const uint160& ci, const uint160& co,
    const uint160& ii, const uint160& io)
{
    bool toXRP = co.isZero();
    ScopedLockType sl (mLock, __FILE__, __LINE__);

    if (toXRP)
    { // We don't want to search through all the to-XRP or from-XRP order books!
        BOOST_FOREACH(OrderBook::ref ob, mSourceMap[currencyIssuer_ct(ci, ii)])
        {
            if ((ob->getCurrencyOut() == co) && (ob->getIssuerOut() == io))
                return;
        }
    }
    else
    {
        BOOST_FOREACH(OrderBook::ref ob, mDestMap[currencyIssuer_ct(co, io)])
        {
            if ((ob->getCurrencyIn() == ci) && (ob->getIssuerIn() == ii))
                return;
        }
    }

    uint256 index = Ledger::getBookBase(ci, ii, co, io);
    OrderBook::pointer book = boost::make_shared<OrderBook> (boost::cref (index),
                              boost::cref (ci), boost::cref (co), boost::cref (ii), boost::cref (io));

    mSourceMap[currencyIssuer_ct (ci, ii)].push_back (book);
    mDestMap[currencyIssuer_ct (co, io)].push_back (book);
    if (toXRP)
        mXRPBooks.insert(currencyIssuer_ct (ci, ii));
}

// return list of all orderbooks that want this issuerID and currencyID
void OrderBookDB::getBooksByTakerPays (const uint160& issuerID, const uint160& currencyID,
                                       std::vector<OrderBook::pointer>& bookRet)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    boost::unordered_map< currencyIssuer_t, std::vector<OrderBook::pointer> >::const_iterator
    it = mSourceMap.find (currencyIssuer_ct (currencyID, issuerID));

    if (it != mSourceMap.end ())
        bookRet = it->second;
    else
        bookRet.clear ();
}

bool OrderBookDB::isBookToXRP(const uint160& issuerID, const uint160& currencyID)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);

    return mXRPBooks.count(currencyIssuer_ct(currencyID, issuerID)) > 0;
}

// return list of all orderbooks that give this issuerID and currencyID
void OrderBookDB::getBooksByTakerGets (const uint160& issuerID, const uint160& currencyID,
                                       std::vector<OrderBook::pointer>& bookRet)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    boost::unordered_map< currencyIssuer_t, std::vector<OrderBook::pointer> >::const_iterator
    it = mDestMap.find (currencyIssuer_ct (currencyID, issuerID));

    if (it != mDestMap.end ())
        bookRet = it->second;
    else
        bookRet.clear ();
}

BookListeners::pointer OrderBookDB::makeBookListeners (const uint160& currencyPays, const uint160& currencyGets,
        const uint160& issuerPays, const uint160& issuerGets)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    BookListeners::pointer ret = getBookListeners (currencyPays, currencyGets, issuerPays, issuerGets);

    if (!ret)
    {
        ret = boost::make_shared<BookListeners> ();
        mListeners[issuerPays][issuerGets][currencyPays][currencyGets] = ret;
    }

    return ret;
}

BookListeners::pointer OrderBookDB::getBookListeners (const uint160& currencyPays, const uint160& currencyGets,
        const uint160& issuerPays, const uint160& issuerGets)
{
    BookListeners::pointer ret;
    ScopedLockType sl (mLock, __FILE__, __LINE__);

    std::map<uint160, std::map<uint160, std::map<uint160, std::map<uint160, BookListeners::pointer> > > >::iterator
    it0 = mListeners.find (issuerPays);

    if (it0 == mListeners.end ())
        return ret;

    std::map<uint160, std::map<uint160, std::map<uint160, BookListeners::pointer> > >::iterator
    it1 = (*it0).second.find (issuerGets);

    if (it1 == (*it0).second.end ())
        return ret;

    std::map<uint160, std::map<uint160, BookListeners::pointer> >::iterator it2 = (*it1).second.find (currencyPays);

    if (it2 == (*it1).second.end ())
        return ret;

    std::map<uint160, BookListeners::pointer>::iterator it3 = (*it2).second.find (currencyGets);

    if (it3 == (*it2).second.end ())
        return ret;

    return (*it3).second;
}

// Based on the meta, send the meta to the streams that are listening
// We need to determine which streams a given meta effects
void OrderBookDB::processTxn (Ledger::ref ledger, const AcceptedLedgerTx& alTx, Json::Value& jvObj)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);

    if (alTx.getResult () == tesSUCCESS)
    {
        // check if this is an offer or an offer cancel or a payment that consumes an offer
        //check to see what the meta looks like
        BOOST_FOREACH (STObject & node, alTx.getMeta ()->getNodes ())
        {
            try
            {
                if (node.getFieldU16 (sfLedgerEntryType) == ltOFFER)
                {
                    SField* field = NULL;

                    if (node.getFName () == sfModifiedNode)
                    {
                        field = &sfPreviousFields;
                    }
                    else if (node.getFName () == sfCreatedNode)
                    {
                        field = &sfNewFields;
                    }
                    else if (node.getFName () == sfDeletedNode)
                    {
                        field = &sfFinalFields;
                    }

                    if (field)
                    {
                        const STObject* data = dynamic_cast<const STObject*> (node.peekAtPField (*field));

                        if (data)
                        {
                            const STAmount& takerGets = data->getFieldAmount (sfTakerGets);
                            const uint160& currencyGets = takerGets.getCurrency ();
                            const uint160& issuerGets = takerGets.getIssuer ();

                            const STAmount& takerPays = data->getFieldAmount (sfTakerPays);
                            const uint160& currencyPays = takerPays.getCurrency ();
                            const uint160& issuerPays = takerPays.getIssuer ();

                            // determine the OrderBook
                            BookListeners::pointer book =
                                getBookListeners (currencyPays, currencyGets, issuerPays, issuerGets);

                            if (book)
                                book->publish (jvObj);
                        }
                    }
                }
            }
            catch (...)
            {
                WriteLog (lsINFO, OrderBookDB) << "Fields not found in OrderBookDB::processTxn";
            }
        }
    }
}
    
//------------------------------------------------------------------------------

BookListeners::BookListeners ()
    : mLock (this, "BookListeners", __FILE__, __LINE__)
{
}

void BookListeners::addSubscriber (InfoSub::ref sub)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    mListeners[sub->getSeq ()] = sub;
}

void BookListeners::removeSubscriber (uint64 seq)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    mListeners.erase (seq);
}

void BookListeners::publish (Json::Value& jvObj)
{
    Json::FastWriter jfwWriter;
    std::string sObj = jfwWriter.write (jvObj);

    ScopedLockType sl (mLock, __FILE__, __LINE__);
    NetworkOPs::SubMapType::const_iterator it = mListeners.begin ();

    while (it != mListeners.end ())
    {
        InfoSub::pointer p = it->second.lock ();

        if (p)
        {
            p->send (jvObj, sObj, true);
            ++it;
        }
        else
            it = mListeners.erase (it);
    }
}

// vim:ts=4
