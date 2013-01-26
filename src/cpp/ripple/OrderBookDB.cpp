#include "OrderBookDB.h"
#include "Log.h"
#include <boost/foreach.hpp>


// TODO: this would be way faster if we could just look under the order dirs
OrderBookDB::OrderBookDB(Ledger::pointer ledger)
{
	// walk through the entire ledger looking for orderbook entries
	uint256 currentIndex=ledger->getFirstLedgerIndex();
	while(currentIndex.isNonZero())
	{
		SLE::pointer entry=ledger->getSLE(currentIndex);

		OrderBook::pointer book=OrderBook::newOrderBook(entry);
		if(book)
		{
			if( mKnownMap.find(book->getBookBase()) != mKnownMap.end() ) 
			{
				mKnownMap[book->getBookBase()]=true;

				if(!book->getCurrencyIn())
				{ // XRP
					mXRPOrders.push_back(book);
				}else
				{
					mIssuerMap[book->getIssuerIn()].push_back(book);
				}
			}
		}

		currentIndex=ledger->getNextLedgerIndex(currentIndex);
	}
}

// return list of all orderbooks that want IssuerID
std::vector<OrderBook::pointer>& OrderBookDB::getBooks(const uint160& issuerID)
{
	if( mIssuerMap.find(issuerID) == mIssuerMap.end() ) return mEmptyVector;
	else return( mIssuerMap[issuerID]); 
}

// return list of all orderbooks that want this issuerID and currencyID
void OrderBookDB::getBooks(const uint160& issuerID, const uint160& currencyID, std::vector<OrderBook::pointer>& bookRet)
{
	if( mIssuerMap.find(issuerID) == mIssuerMap.end() )
	{
		BOOST_FOREACH(OrderBook::ref book, mIssuerMap[issuerID])
		{
			if(book->getCurrencyIn()==currencyID)
			{
				bookRet.push_back(book);
			}
		}
	}
}