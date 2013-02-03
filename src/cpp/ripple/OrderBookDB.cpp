#include <boost/foreach.hpp>

#include "OrderBookDB.h"
#include "Log.h"

SETUP_LOG();

// TODO: this would be way faster if we could just look under the order dirs
OrderBookDB::OrderBookDB(Ledger::pointer ledger)
{
	// walk through the entire ledger looking for orderbook entries
	uint256 currentIndex = ledger->getFirstLedgerIndex();

	cLog(lsDEBUG) << "OrderBookDB>";

	while (currentIndex.isNonZero())
	{
		SLE::pointer entry=ledger->getSLE(currentIndex);

		OrderBook::pointer book = OrderBook::newOrderBook(entry);
		if (book)
		{
			cLog(lsDEBUG) << "OrderBookDB: found book";

			if (mKnownMap.find(book->getBookBase()) == mKnownMap.end())
			{
				mKnownMap[book->getBookBase()] = true;

				cLog(lsDEBUG) << "OrderBookDB: unknown book in: "
					<< STAmount::createHumanCurrency(book->getCurrencyIn())
					<< " -> "
					<< STAmount::createHumanCurrency(book->getCurrencyOut());

				if (!book->getCurrencyIn())
				{
					// XRP
					mXRPOrders.push_back(book);
				}
				else
				{
					mIssuerMap[book->getIssuerIn()].push_back(book);
				}
			}
		}

		currentIndex=ledger->getNextLedgerIndex(currentIndex);
	}

	cLog(lsDEBUG) << "OrderBookDB<";
}

// return list of all orderbooks that want IssuerID
std::vector<OrderBook::pointer>& OrderBookDB::getBooks(const uint160& issuerID)
{
	return mIssuerMap.find(issuerID) == mIssuerMap.end()
		? mEmptyVector
		: mIssuerMap[issuerID];
}

// return list of all orderbooks that want this issuerID and currencyID
void OrderBookDB::getBooks(const uint160& issuerID, const uint160& currencyID, std::vector<OrderBook::pointer>& bookRet)
{
	if (mIssuerMap.find(issuerID) == mIssuerMap.end())
	{
		BOOST_FOREACH(OrderBook::ref book, mIssuerMap[issuerID])
		{
			if (book->getCurrencyIn() == currencyID)
				bookRet.push_back(book);
		}
	}
}

// vim:ts=4
