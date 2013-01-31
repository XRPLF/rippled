#include "OrderBookDB.h"
#include "Log.h"
#include <boost/foreach.hpp>


OrderBookDB::OrderBookDB()
{

}

// TODO: this would be way faster if we could just look under the order dirs
void OrderBookDB::setup(Ledger::pointer ledger)
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

OrderBook::pointer OrderBookDB::getBook(uint160 mCurrencyIn, uint160 mCurrencyOut, uint160 mIssuerIn, uint160 mIssuerOut)
{

}

/*
"CreatedNode" : {
"LedgerEntryType" : "Offer",
"LedgerIndex" : "F353BF8A7DCE35EA2985596F4C8421E30EF3B9A21618566BFE0ED00B62A8A5AB",
"NewFields" : {
"Account" : "rB5TihdPbKgMrkFqrqUC3yLdE8hhv4BdeY",
"BookDirectory" : "FF26BE244767D0EA9EFD523941439009E4185E4CBB918F714C08E1BC9BF04000",
"Sequence" : 112,
"TakerGets" : "400000000",
"TakerPays" : {
"currency" : "BTC",
"issuer" : "r3kmLJN5D28dHuH8vZNUZpMC43pEHpaocV",
"value" : "1"
}
}
}*/

void OrderBookDB::processTxn(const SerializedTransaction& stTxn, TER terResult,TransactionMetaSet::pointer& meta,Json::Value& jvObj)
{
	// check if this is an offer or an offer cancel or a payment that consumes an offer
	//check to see what the meta looks like
	BOOST_FOREACH(STObject& node,meta->getNodes())
	{
		if(node.getFieldU16(sfLedgerEntryType)==ltOFFER)
		{

		}
	}
	

}