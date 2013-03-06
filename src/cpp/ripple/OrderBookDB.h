
#ifndef ORDERBOOK_DB_H
#define ORDERBOOK_DB_H

#include <boost/shared_ptr.hpp>
#include <boost/unordered_map.hpp>

#include "Ledger.h"
#include "AcceptedLedger.h"
#include "OrderBook.h"


//
// XXX Eventually make this cached and just update it as transactions come in.
// But, for now it is probably faster to just generate it each time.
//

class BookListeners
{
	boost::unordered_map<uint64, InfoSub::wptr> mListeners;
	boost::recursive_mutex mLock;

public:
	typedef boost::shared_ptr<BookListeners> pointer;

	void addSubscriber(InfoSub::ref sub);
	void removeSubscriber(uint64 sub);
	void publish(Json::Value& jvObj);
};

class OrderBookDB
{
	std::vector<OrderBook::pointer> mEmptyVector;
	std::vector<OrderBook::pointer> mXRPOrders;
	boost::unordered_map<uint160, std::vector<OrderBook::pointer> > mIssuerMap;
	//std::vector<OrderBook::pointer> mAllOrderBooks;

	// issuerIn, issuerOut, currencyIn, currencyOut
	std::map<uint160, std::map<uint160, std::map<uint160, std::map<uint160, BookListeners::pointer> > > > mListeners; 

	uint32 mSeq;
	boost::recursive_mutex mLock;

public:
	OrderBookDB();
	void setup(Ledger::ref ledger);
	void invalidate();

	// return list of all orderbooks that want XRP
	std::vector<OrderBook::pointer>& getXRPInBooks(){ return mXRPOrders; }

	// return list of all orderbooks that want IssuerID
	std::vector<OrderBook::pointer>& getBooks(const uint160& issuerID);

	// return list of all orderbooks that want this issuerID and currencyID
	void getBooks(const uint160& issuerID, const uint160& currencyID, std::vector<OrderBook::pointer>& bookRet);

	// returns the best rate we can find
	float getPrice(uint160& currencyIn,uint160& currencyOut);


	BookListeners::pointer getBookListeners(const uint160& currencyIn, const uint160& currencyOut,
		const uint160& issuerIn, const uint160& issuerOut);
	BookListeners::pointer makeBookListeners(const uint160& currencyIn, const uint160& currencyOut,
		const uint160& issuerIn, const uint160& issuerOut);

	// see if this txn effects any orderbook
	void processTxn(Ledger::ref ledger, const ALTransaction& alTx, Json::Value& jvObj);

};

#endif

// vim:ts=4
