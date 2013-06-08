
#ifndef ORDERBOOK_H
#define ORDERBOOK_H

/*
	Encapsulates the SLE for an orderbook
*/
class OrderBook
{
public:
	typedef boost::shared_ptr<OrderBook> pointer;
	typedef const boost::shared_ptr<OrderBook>& ref;

	OrderBook(uint256 const& index, const uint160& ci, const uint160& co, const uint160& ii, const uint160& io) :
		mBookBase(index),
		mCurrencyIn(ci),
		mCurrencyOut(co),
		mIssuerIn(ii),
		mIssuerOut(io)
	{ ; }

	uint256& getBookBase(){ return(mBookBase); }
	uint160& getCurrencyIn(){ return(mCurrencyIn); }
	uint160& getCurrencyOut(){ return(mCurrencyOut); }
	uint160& getIssuerIn(){ return(mIssuerIn); }
	uint160& getIssuerOut(){ return(mIssuerOut); }

	// looks through the best offers to see how much it would cost to take the given amount
	STAmount& getTakePrice(STAmount& takeAmount);

private:
	uint256 mBookBase;

	uint160 mCurrencyIn;
	uint160 mCurrencyOut;
	uint160 mIssuerIn;
	uint160 mIssuerOut;

	//SerializedLedgerEntry::pointer	mLedgerEntry;
	OrderBook(SerializedLedgerEntry::ref ledgerEntry);	// For accounts in a ledger
};

#endif

// vim:ts=4
