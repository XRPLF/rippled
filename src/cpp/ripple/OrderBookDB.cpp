#include <boost/foreach.hpp>

#include "OrderBookDB.h"
#include "Log.h"

SETUP_LOG();

OrderBookDB::OrderBookDB()
{

}

// TODO: this would be way faster if we could just look under the order dirs
void OrderBookDB::setup(Ledger::pointer ledger)
{
	mXRPOrders.clear();
	mIssuerMap.clear();
	mKnownMap.clear();

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

BookListeners::pointer OrderBookDB::makeBookListeners(uint160 currencyIn, uint160 currencyOut, uint160 issuerIn, uint160 issuerOut)
{
	BookListeners::pointer ret=getBookListeners(currencyIn, currencyOut, issuerIn, issuerOut);
	if(!ret)
	{
		ret=BookListeners::pointer(new BookListeners);
		mListeners[issuerIn][issuerOut][currencyIn][currencyOut]=ret;
	}
	return(ret);
}

BookListeners::pointer OrderBookDB::getBookListeners(uint160 currencyIn, uint160 currencyOut, uint160 issuerIn, uint160 issuerOut)
{
	std::map<uint160, std::map<uint160, std::map<uint160, std::map<uint160, BookListeners::pointer> > > >::iterator it0=mListeners.find(issuerIn);
	if(it0 != mListeners.end())
	{
		std::map<uint160, std::map<uint160, std::map<uint160, BookListeners::pointer> > >::iterator it1=(*it0).second.find(issuerOut);
		if(it1 != (*it0).second.end())
		{
			std::map<uint160, std::map<uint160, BookListeners::pointer> >::iterator it2=(*it1).second.find(currencyIn);
			if(it2 != (*it1).second.end())
			{
				std::map<uint160, BookListeners::pointer>::iterator it3=(*it2).second.find(currencyOut);
				if(it3 != (*it2).second.end())
				{
					return( (*it3).second );
				}
			}
		}
	}
	return(BookListeners::pointer());
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
}

"ModifiedNode" : {
"FinalFields" : {
"Account" : "rHTxKLzRbniScyQFGMb3NodmxA848W8dKM",
"BookDirectory" : "407AF8FFDE71114B1981574FDDA9B0334572D56FC579735B4B0BD7A625405555",
"BookNode" : "0000000000000000",
"Flags" : 0,
"OwnerNode" : "0000000000000000",
"Sequence" : 32,
"TakerGets" : "149900000000",
"TakerPays" : {
"currency" : "USD",
"issuer" : "r9vbV3EHvXWjSkeQ6CAcYVPGeq7TuiXY2X",
"value" : "49.96666666666667"
}
},
"LedgerEntryType" : "Offer",
"LedgerIndex" : "C60F8CC514208FA5F7BD03CF1B64B38B7183CD52318FCBBD3726350D4FE693B0",
"PreviousFields" : {
"TakerGets" : "150000000000",
"TakerPays" : {
"currency" : "USD",
"issuer" : "r9vbV3EHvXWjSkeQ6CAcYVPGeq7TuiXY2X",
"value" : "50"
}
},
"PreviousTxnID" : "1A6AAE3F1AC5A8A7554A5ABC395D17FED5BF62CD90181AA8E4315EDFED4EDEB3",
"PreviousTxnLgrSeq" : 140734
}

*/
// Based on the meta, send the meta to the streams that are listening 
// We need to determine which streams a given meta effects
void OrderBookDB::processTxn(const SerializedTransaction& stTxn, TER terResult,TransactionMetaSet::pointer& meta,Json::Value& jvObj)
{
	if(terResult==tesSUCCESS)
	{
		// check if this is an offer or an offer cancel or a payment that consumes an offer
		//check to see what the meta looks like
		BOOST_FOREACH(STObject& node,meta->getNodes())
		{
			try{
				if(node.getFieldU16(sfLedgerEntryType)==ltOFFER)
				{
					SField* field=NULL;

					if(node.getFName() == sfModifiedNode)
					{
						field=&sfPreviousFields;
					}else if(node.getFName() == sfCreatedNode)
					{
						field=&sfNewFields;
					}

					const STObject* previous = dynamic_cast<const STObject*>(node.peekAtPField(*field));
					if(previous)
					{
						STAmount takerGets = previous->getFieldAmount(sfTakerGets);
						uint160 currencyOut=takerGets.getCurrency();
						uint160 issuerOut=takerGets.getIssuer();

						STAmount takerPays = previous->getFieldAmount(sfTakerPays);
						uint160 currencyIn=takerPays.getCurrency();
						uint160 issuerIn=takerPays.getIssuer();

						// determine the OrderBook
						BookListeners::pointer book=getBookListeners(currencyIn,currencyOut,issuerIn,issuerOut);
						if(book) book->publish(jvObj);
					}
				}
			}catch(...)
			{
				cLog(lsINFO) << "Fields not found in OrderBookDB::processTxn";
			}
		}
	}
}

void BookListeners::addSubscriber(InfoSub* sub)
{
	mListeners.insert(sub);
}

void BookListeners::removeSubscriber(InfoSub* sub)
{
	mListeners.erase(sub);
}

void BookListeners::publish(Json::Value& jvObj)
{
	//Json::Value jvObj=node.getJson(0);

	BOOST_FOREACH(InfoSub* sub,mListeners)
	{
		sub->send(jvObj);
	}
}

// vim:ts=4
