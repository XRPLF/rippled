#include "Ledger.h"
#include "OrderBook.h"

/* 
we can eventually make this cached and just update it as transactions come in. 
But for now it is probably faster to just generate it each time
*/

class OrderBookDB
{
	std::vector<OrderBook::pointer> mEmptyVector;
	std::vector<OrderBook::pointer> mXNSOrders;
	std::map<uint160, std::vector<OrderBook::pointer> > mIssuerMap;

	std::map<uint256, bool >  mKnownMap;

public:
	OrderBookDB(Ledger::pointer ledger);

	// return list of all orderbooks that want XNS 
	std::vector<OrderBook::pointer>& getXNSInBooks(){ return mXNSOrders; }
	// return list of all orderbooks that want IssuerID
	std::vector<OrderBook::pointer>& getBooks(const uint160& issuerID);
	// return list of all orderbooks that want this issuerID and currencyID
	void getBooks(const uint160& issuerID, const uint160& currencyID, std::vector<OrderBook::pointer>& bookRet);

	// returns the best rate we can find
	float getPrice(uint160& currencyIn,uint160& currencyOut);

};