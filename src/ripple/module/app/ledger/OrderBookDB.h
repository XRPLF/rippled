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

#ifndef RIPPLE_ORDERBOOKDB_H_INCLUDED
#define RIPPLE_ORDERBOOKDB_H_INCLUDED

namespace ripple {

// VFALCO TODO Add Javadoc comment explaining what this class does
class BookListeners
{
public:
    typedef std::shared_ptr<BookListeners> pointer;

    BookListeners ();
    void addSubscriber (InfoSub::ref sub);
    void removeSubscriber (std::uint64_t sub);
    void publish (Json::Value const& jvObj);

private:
    typedef RippleRecursiveMutex LockType;
    typedef std::lock_guard <LockType> ScopedLockType;
    LockType mLock;

    // VFALCO TODO Use a typedef for the uint64
    //             Use a typedef for the container
    ripple::unordered_map<std::uint64_t, InfoSub::wptr> mListeners;
};

//------------------------------------------------------------------------------

// VFALCO TODO Add Javadoc comment explaining what this class does
class OrderBookDB
    : public beast::Stoppable
    , public beast::LeakChecked <OrderBookDB>
{
public:
    explicit OrderBookDB (Stoppable& parent);

    void setup (Ledger::ref ledger);
    void update (Ledger::pointer ledger);
    void invalidate ();

    void addOrderBook(const uint160& takerPaysCurrency, const uint160& takerGetsCurrency,
        const uint160& takerPaysIssuer, const uint160& takerGetsIssuer);

    // return list of all orderbooks that want this issuerID and currencyID
    void getBooksByTakerPays (RippleIssuer const& issuerID, RippleCurrency const& currencyID,
                              std::vector<OrderBook::pointer>& bookRet);
    void getBooksByTakerGets (RippleIssuer const& issuerID, RippleCurrency const& currencyID,
                              std::vector<OrderBook::pointer>& bookRet);

    bool isBookToXRP (RippleIssuer const& issuerID, RippleCurrency const& currencyID);

    BookListeners::pointer getBookListeners (RippleCurrency const& currencyPays, RippleCurrency const& currencyGets,
            RippleIssuer const& issuerPays, RippleIssuer const& issuerGets);

    BookListeners::pointer makeBookListeners (RippleCurrency const& currencyPays, RippleCurrency const& currencyGets,
            RippleIssuer const& issuerPays, RippleIssuer const& issuerGets);

    // see if this txn effects any orderbook
    void processTxn (Ledger::ref ledger, const AcceptedLedgerTx& alTx, Json::Value const& jvObj);

private:
    // by ci/ii
    ripple::unordered_map <RippleAsset,
        std::vector <OrderBook::pointer>> mSourceMap;

    // by co/io
    ripple::unordered_map <RippleAsset,
        std::vector<OrderBook::pointer>> mDestMap;

    // does an order book to XRP exist
    boost::unordered_set <RippleAsset> mXRPBooks;

    typedef RippleRecursiveMutex LockType;
    typedef std::lock_guard <LockType> ScopedLockType;
    LockType mLock;

    typedef ripple::unordered_map <RippleBook, BookListeners::pointer> MapType;

    MapType mListeners;

    std::uint32_t mSeq;

};

} // ripple

#endif
