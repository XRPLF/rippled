#include "Ledger.h"
#include "OrderBook.h"

/* 
we can eventually make this cached and just update it as transactions come in. 
But for now it is probably faster to just generate it each time
*/

class OrderBookDB
{
	std::vector<OrderBook::pointer> mEmptyVector;
	std::vector<OrderBook::pointer> mXRPOrders;
	std::map<uint160, std::vector<OrderBook::pointer> > mIssuerMap;
	//std::vector<OrderBook::pointer> mAllOrderBooks;

	std::map<uint256, bool >  mKnownMap;

public:
	OrderBookDB();
	void setup(Ledger::pointer ledger);

	// return list of all orderbooks that want XRP
	std::vector<OrderBook::pointer>& getXRPInBooks(){ return mXRPOrders; }
	// return list of all orderbooks that want IssuerID
	std::vector<OrderBook::pointer>& getBooks(const uint160& issuerID);
	// return list of all orderbooks that want this issuerID and currencyID
	void getBooks(const uint160& issuerID, const uint160& currencyID, std::vector<OrderBook::pointer>& bookRet);

	// returns the best rate we can find
	float getPrice(uint160& currencyIn,uint160& currencyOut);


	OrderBook::pointer getBook(uint160 mCurrencyIn, uint160 mCurrencyOut, uint160 mIssuerIn, uint160 mIssuerOut);
	// see if this txn effects any orderbook
	void processTxn(const SerializedTransaction& stTxn, TER terResult,TransactionMetaSet::pointer& meta,Json::Value& jvObj);

};

// vim:ts=4
