#include "SerializedLedger.h"
#include <boost/shared_ptr.hpp>
/*
	Encapsulates the SLE for an orderbook
*/
class OrderBook
{
	uint256 mBookBase;

	uint160 mCurrencyIn;
	uint160 mCurrencyOut;
	uint160 mIssuerIn;
	uint160 mIssuerOut;

	//SerializedLedgerEntry::pointer	mLedgerEntry;
	OrderBook(SerializedLedgerEntry::pointer ledgerEntry);	// For accounts in a ledger
public:
	typedef boost::shared_ptr<OrderBook> pointer;
	typedef const boost::shared_ptr<OrderBook>& ref;

	// returns NULL if ledgerEntry doesn't point to an order
	// if ledgerEntry is an Order it creates the OrderBook this order would live in
	static OrderBook::pointer newOrderBook(SerializedLedgerEntry::pointer ledgerEntry);

	uint256& getBookBase(){ return(mBookBase); }
	uint160& getCurrencyIn(){ return(mCurrencyIn); }
	uint160& getCurrencyOut(){ return(mCurrencyOut); }
	uint160& getIssuerIn(){ return(mIssuerIn); }
	uint160& getIssuerOut(){ return(mIssuerOut); }

	// looks through the best offers to see how much it would cost to take the given amount
	STAmount& getTakePrice(STAmount& takeAmount);
	

};