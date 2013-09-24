//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef ORDERBOOK_DB_H
#define ORDERBOOK_DB_H

//
// XXX Eventually make this cached and just update it as transactions come in.
// But, for now it is probably faster to just generate it each time.
//

//------------------------------------------------------------------------------

typedef std::pair<uint160, uint160> currencyIssuer_t;

//------------------------------------------------------------------------------

#ifdef C11X
typedef std::pair<const uint160&, const uint160&> currencyIssuer_ct;
#else
typedef std::pair<uint160, uint160> currencyIssuer_ct; // C++ defect 106
#endif

//------------------------------------------------------------------------------

class BookListeners
{
public:
    typedef boost::shared_ptr<BookListeners> pointer;

    BookListeners ();
    void addSubscriber (InfoSub::ref sub);
    void removeSubscriber (uint64 sub);
    void publish (Json::Value& jvObj);

private:
    typedef RippleRecursiveMutex LockType;
    typedef LockType::ScopedLockType ScopedLockType;
    LockType mLock;
    boost::unordered_map<uint64, InfoSub::wptr> mListeners;
};

//------------------------------------------------------------------------------

class OrderBookDB
    : public Stoppable
    , public LeakChecked <OrderBookDB>
{
public:
    explicit OrderBookDB (Stoppable& parent);

    void setup (Ledger::ref ledger);
    void update (Ledger::pointer ledger);
    void invalidate ();

    void addOrderBook(const uint160& takerPaysCurrency, const uint160& takerGetsCurrency,
        const uint160& takerPaysIssuer, const uint160& takerGetsIssuer);

    // return list of all orderbooks that want this issuerID and currencyID
    void getBooksByTakerPays (const uint160& issuerID, const uint160& currencyID,
                              std::vector<OrderBook::pointer>& bookRet);
    void getBooksByTakerGets (const uint160& issuerID, const uint160& currencyID,
                              std::vector<OrderBook::pointer>& bookRet);

    bool isBookToXRP (const uint160& issuerID, const uint160& currencyID);

    BookListeners::pointer getBookListeners (const uint160& currencyPays, const uint160& currencyGets,
            const uint160& issuerPays, const uint160& issuerGets);

    BookListeners::pointer makeBookListeners (const uint160& currencyPays, const uint160& currencyGets,
            const uint160& issuerPays, const uint160& issuerGets);

    // see if this txn effects any orderbook
    void processTxn (Ledger::ref ledger, const AcceptedLedgerTx& alTx, Json::Value& jvObj);

private:
    boost::unordered_map< currencyIssuer_t, std::vector<OrderBook::pointer> > mSourceMap;   // by ci/ii
    boost::unordered_map< currencyIssuer_t, std::vector<OrderBook::pointer> > mDestMap;     // by co/io
    boost::unordered_set< currencyIssuer_t > mXRPBooks; // does an order book to XRP exist
    typedef RippleRecursiveMutex LockType;
    typedef LockType::ScopedLockType ScopedLockType;
    LockType mLock;


    // issuerPays, issuerGets, currencyPays, currencyGets
    std::map<uint160, std::map<uint160, std::map<uint160, std::map<uint160, BookListeners::pointer> > > > mListeners;

    uint32 mSeq;

};

#endif

// vim:ts=4
