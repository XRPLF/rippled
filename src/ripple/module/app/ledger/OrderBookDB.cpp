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

SETUP_LOG (OrderBookDB)

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
    {
        ScopedLockType sl (mLock);

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
            std::bind(&OrderBookDB::update, this, ledger));
}

static void updateHelper (SLE::ref entry,
    boost::unordered_set< uint256 >& seen,
    ripple::unordered_map< RippleAsset, std::vector<OrderBook::pointer> >& destMap,
    ripple::unordered_map< RippleAsset, std::vector<OrderBook::pointer> >& sourceMap,
    boost::unordered_set< RippleAsset >& XRPBooks,
    int& books)
{
    if ((entry->getType () == ltDIR_NODE) && (entry->isFieldPresent (sfExchangeRate)) &&
            (entry->getFieldH256 (sfRootIndex) == entry->getIndex()))
    {
        const uint160& ci = entry->getFieldH160 (sfTakerPaysCurrency);
        const uint160& co = entry->getFieldH160 (sfTakerGetsCurrency);
        const uint160& ii = entry->getFieldH160 (sfTakerPaysIssuer);
        const uint160& io = entry->getFieldH160 (sfTakerGetsIssuer);

        uint256 index = Ledger::getBookBase (ci, ii, co, io);

        if (seen.insert (index).second)
        {
            // VFALCO TODO Reduce the clunkiness of these parameter wrappers
            OrderBook::pointer book = std::make_shared<OrderBook> (std::cref (index),
                                      std::cref (ci), std::cref (co), std::cref (ii), std::cref (io));

            sourceMap[RippleAssetRef (ci, ii)].push_back (book);
            destMap[RippleAssetRef (co, io)].push_back (book);
            if (co.isZero())
                XRPBooks.insert(RippleAssetRef (ci, ii));
            ++books;
        }
    }
}

void OrderBookDB::update (Ledger::pointer ledger)
{
    boost::unordered_set< uint256 > seen;
    ripple::unordered_map< RippleAsset, std::vector<OrderBook::pointer> > destMap;
    ripple::unordered_map< RippleAsset, std::vector<OrderBook::pointer> > sourceMap;
    boost::unordered_set< RippleAsset > XRPBooks;

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
        WriteLog (lsINFO, OrderBookDB) << "OrderBookDB::update encountered a missing node";
        ScopedLockType sl (mLock);
        mSeq = 0;
        return;
    }

    WriteLog (lsDEBUG, OrderBookDB) << "OrderBookDB::update< " << books << " books found";
    {
        ScopedLockType sl (mLock);

        mXRPBooks.swap(XRPBooks);
        mSourceMap.swap(sourceMap);
        mDestMap.swap(destMap);
    }
    getApp().getLedgerMaster().newOrderBookDB();
}

void OrderBookDB::addOrderBook(const uint160& ci, const uint160& co,
    const uint160& ii, const uint160& io)
{
    bool toXRP = co.isZero();
    ScopedLockType sl (mLock);

    if (toXRP)
    { // We don't want to search through all the to-XRP or from-XRP order books!
        for (auto ob : mSourceMap[RippleAssetRef(ci, ii)])
        {
            if (ob->getCurrencyOut().isZero ()) // also to XRP
                return;
        }
    }
    else
    {
        for (auto ob : mDestMap[RippleAssetRef(co, io)])
        {
            if ((ob->getCurrencyIn() == ci) && (ob->getIssuerIn() == ii))
                return;
        }
    }

    uint256 index = Ledger::getBookBase(ci, ii, co, io);
    OrderBook::pointer book = std::make_shared<OrderBook> (std::cref (index),
                              std::cref (ci), std::cref (co), std::cref (ii), std::cref (io));

    mSourceMap[RippleAssetRef (ci, ii)].push_back (book);
    mDestMap[RippleAssetRef (co, io)].push_back (book);
    if (toXRP)
        mXRPBooks.insert(RippleAssetRef (ci, ii));
}

// return list of all orderbooks that want this issuerID and currencyID
void OrderBookDB::getBooksByTakerPays (RippleIssuer const& issuerID, RippleCurrency const& currencyID,
                                       std::vector<OrderBook::pointer>& bookRet)
{
    ScopedLockType sl (mLock);
    auto it = mSourceMap.find (RippleAssetRef (currencyID, issuerID));

    if (it != mSourceMap.end ())
        bookRet = it->second;
    else
        bookRet.clear ();
}

bool OrderBookDB::isBookToXRP(RippleIssuer const& issuerID, RippleCurrency const& currencyID)
{
    ScopedLockType sl (mLock);

    return mXRPBooks.count(RippleAssetRef(currencyID, issuerID)) > 0;
}

// return list of all orderbooks that give this issuerID and currencyID
void OrderBookDB::getBooksByTakerGets (RippleIssuer const& issuerID, RippleCurrency const& currencyID,
                                       std::vector<OrderBook::pointer>& bookRet)
{
    ScopedLockType sl (mLock);
    auto it = mDestMap.find (RippleAssetRef (currencyID, issuerID));

    if (it != mDestMap.end ())
        bookRet = it->second;
    else
        bookRet.clear ();
}

BookListeners::pointer OrderBookDB::makeBookListeners (RippleCurrency const& currencyPays, RippleCurrency const& currencyGets,
        RippleIssuer const& issuerPays, RippleIssuer const& issuerGets)
{
    ScopedLockType sl (mLock);
    BookListeners::pointer ret = getBookListeners (currencyPays, currencyGets, issuerPays, issuerGets);

    if (!ret)
    {
        ret = std::make_shared<BookListeners> ();

        mListeners [RippleBookRef (
            RippleAssetRef (currencyPays, issuerPays),
            RippleAssetRef (currencyGets, issuerGets))] = ret;
        assert (getBookListeners (currencyPays, currencyGets, issuerPays, issuerGets) == ret);
    }

    return ret;
}

BookListeners::pointer OrderBookDB::getBookListeners (RippleCurrency const& currencyPays, RippleCurrency const& currencyGets,
        RippleIssuer const& issuerPays, RippleIssuer const& issuerGets)
{
    BookListeners::pointer ret;
    ScopedLockType sl (mLock);

    MapType::iterator it0 (mListeners.find (RippleBookRef (
        RippleAssetRef (currencyPays, issuerPays),
            RippleAssetRef (currencyGets, issuerGets))));

    if (it0 != mListeners.end ())
        ret = it0->second;

    return ret;
}

// Based on the meta, send the meta to the streams that are listening
// We need to determine which streams a given meta effects
void OrderBookDB::processTxn (Ledger::ref ledger, const AcceptedLedgerTx& alTx, Json::Value const& jvObj)
{
    ScopedLockType sl (mLock);

    if (alTx.getResult () == tesSUCCESS)
    {
        // check if this is an offer or an offer cancel or a payment that consumes an offer
        //check to see what the meta looks like
        for (auto& node : alTx.getMeta ()->getNodes ())
        {
            try
            {
                if (node.getFieldU16 (sfLedgerEntryType) == ltOFFER)
                {
                    SField* field = nullptr;

                    // We need a field that contains the TakerGets and TakerPays parameters
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
                            RippleCurrency const& currencyGets = takerGets.getCurrency ();
                            RippleIssuer const& issuerGets = takerGets.getIssuer ();

                            const STAmount& takerPays = data->getFieldAmount (sfTakerPays);
                            RippleCurrency const& currencyPays = takerPays.getCurrency ();
                            RippleIssuer const& issuerPays = takerPays.getIssuer ();

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
{
}

void BookListeners::addSubscriber (InfoSub::ref sub)
{
    ScopedLockType sl (mLock);
    mListeners[sub->getSeq ()] = sub;
}

void BookListeners::removeSubscriber (std::uint64_t seq)
{
    ScopedLockType sl (mLock);
    mListeners.erase (seq);
}

void BookListeners::publish (Json::Value const& jvObj)
{
    Json::FastWriter jfwWriter;
    std::string sObj = jfwWriter.write (jvObj);

    ScopedLockType sl (mLock);
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

} // ripple
