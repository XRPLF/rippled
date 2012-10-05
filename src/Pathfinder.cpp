#include "Pathfinder.h"
#include "Application.h"
#include "RippleLines.h"
#include "Log.h"
#include <boost/foreach.hpp>

/*
JED: V IIII

we just need to find a succession of the highest quality paths there until we find enough width

Don't do branching within each path

We have a list of paths we are working on but how do we compare the ones that are terminating in a different currency?

Loops

TODO: what is a good way to come up with multiple paths?
	Maybe just change the sort criteria?
	first a low cost one and then a fat short one?


OrderDB:
	getXNSOffers();

	// return list of all orderbooks that want XNS
	// return list of all orderbooks that want IssuerID
	// return list of all orderbooks that want this issuerID and currencyID
*/

/*
Test sending to XNS
Test XNS to XNS
Test offer in middle
Test XNS to USD
Test USD to EUR
*/


// we sort the options by:
//    cost of path
//    length of path
//    width of path
//    correct currency at the end



bool sortPathOptions(PathOption::pointer first, PathOption::pointer second)
{
	if(first->mTotalCost<second->mTotalCost) return(true);
	if(first->mTotalCost>second->mTotalCost) return(false);

	if(first->mCorrectCurrency && !second->mCorrectCurrency) return(true);
	if(!first->mCorrectCurrency && second->mCorrectCurrency) return(false);

	if(first->mPath.getElementCount()<second->mPath.getElementCount()) return(true);
	if(first->mPath.getElementCount()>second->mPath.getElementCount()) return(false);

	if(first->mMinWidth<second->mMinWidth) return true;

	return false;
}

PathOption::PathOption(uint160& srcAccount,uint160& srcCurrencyID,const uint160& dstCurrencyID)
{
	mCurrentAccount=srcAccount;
	mCurrencyID=srcCurrencyID;
	mCorrectCurrency=(srcCurrencyID==dstCurrencyID);
	mQuality=0;
	mMinWidth=STAmount(dstCurrencyID,99999,80);   // this will get lowered when we convert back to the correct currency
}

PathOption::PathOption(PathOption::pointer other)
{
	// TODO:
}


Pathfinder::Pathfinder(NewcoinAddress& srcAccountID, NewcoinAddress& dstAccountID, uint160& srcCurrencyID, STAmount dstAmount) : 
	mSrcAccountID(srcAccountID.getAccountID()), mDstAccountID(dstAccountID.getAccountID()), mDstAmount(dstAmount), mSrcCurrencyID(srcCurrencyID), mOrderBook(theApp->getMasterLedger().getCurrentLedger())
{
	mLedger=theApp->getMasterLedger().getCurrentLedger();
}

bool Pathfinder::findPaths(int maxSearchSteps, int maxPay, STPathSet& retPathSet)
{
  if(mLedger) {
    std::queue<STPath> pqueue;
    STPathElement ele(mSrcAccountID, 
		      mSrcCurrencyID,
		      uint160());
    STPath path;
    path.addElement(ele);
    pqueue.push(path);
    while(pqueue.size()) {

      STPath path = pqueue.front();
      pqueue.pop();
      // get the first path from the queue

      ele = path.mPath.back();
      // get the last node from the path

      if (ele.mAccountID == mDstAccountID) {
	path.mPath.erase(path.mPath.begin());
	path.mPath.erase(path.mPath.begin() + path.mPath.size()-1);
	retPathSet.addPath(path);
	return true;
      } 
      // found the destination

      if (!ele.mCurrencyID) {
	BOOST_FOREACH(OrderBook::pointer book,mOrderBook.getXNSInBooks())
	  {
	    //if (!path.hasSeen(line->getAccountIDPeer().getAccountID())) 
	      {

		STPath new_path(path);
		STPathElement new_ele(uint160(), book->getCurrencyOut(), book->getIssuerOut());
		new_path.mPath.push_back(new_ele);
		pqueue.push(new_path);
	      }
	  }

      } else {
	RippleLines rippleLines(ele.mAccountID);
	BOOST_FOREACH(RippleState::pointer line,rippleLines.getLines())
	  {
	    if (!path.hasSeen(line->getAccountIDPeer().getAccountID())) 
	      {
		STPath new_path(path);
		STPathElement new_ele(line->getAccountIDPeer().getAccountID(),
				      ele.mCurrencyID,
				      uint160());
	
		new_path.mPath.push_back(new_ele);
		pqueue.push(new_path);
	      }
	  }
      }
      // enumerate all adjacent nodes, construct a new path and push it into the queue
    } // While
  } // if there is a ledger

  return false;
}

bool Pathfinder::checkComplete(STPathSet& retPathSet)
{
	if(mCompletePaths.size())
	{ // TODO: look through these and pick the most promising
		int count=0;
		BOOST_FOREACH(PathOption::pointer pathOption,mCompletePaths)
		{
			retPathSet.addPath(pathOption->mPath);
			count++;
			if(count>2) return(true);
		}
		return(true);
	}
	return(false);
}


// get all the options from this accountID
//   if source is XNS
//		every offer that wants XNS
//   else
//		every ripple line that starts with the source currency
//		every offer that we can take that wants the source currency

void Pathfinder::addOptions(PathOption::pointer tail)
{
	if(!tail->mCurrencyID)
	{ // source XNS
		BOOST_FOREACH(OrderBook::pointer book,mOrderBook.getXNSInBooks())
		{
			PathOption::pointer pathOption(new PathOption(tail));

			STPathElement ele(uint160(), book->getCurrencyOut(), book->getIssuerOut());
			pathOption->mPath.addElement(ele);

			pathOption->mCurrentAccount=book->getIssuerOut();
			pathOption->mCurrencyID=book->getCurrencyOut();
			addPathOption(pathOption);
		}
	}else
	{ // ripple
		RippleLines rippleLines(tail->mCurrentAccount);
		BOOST_FOREACH(RippleState::pointer line,rippleLines.getLines())
		{
			// TODO: make sure we can move in the correct direction
			STAmount balance=line->getBalance(); 
			if(balance.getCurrency()==tail->mCurrencyID)
			{  // we have a ripple line from the tail to somewhere else
				PathOption::pointer pathOption(new PathOption(tail));

				STPathElement ele(line->getAccountIDPeer().getAccountID(), uint160(), uint160());
				pathOption->mPath.addElement(ele);


				pathOption->mCurrentAccount=line->getAccountIDPeer().getAccountID();
				addPathOption(pathOption);
			}
		}

		// every offer that wants the source currency
		std::vector<OrderBook::pointer> books;
		mOrderBook.getBooks(tail->mCurrentAccount, tail->mCurrencyID, books);

		BOOST_FOREACH(OrderBook::pointer book,books)
		{
			PathOption::pointer pathOption(new PathOption(tail));

			STPathElement ele(uint160(), book->getCurrencyOut(), book->getIssuerOut());
			pathOption->mPath.addElement(ele);

			pathOption->mCurrentAccount=book->getIssuerOut();
			pathOption->mCurrencyID=book->getCurrencyOut();
			addPathOption(pathOption);
		}
	}
}

void Pathfinder::addPathOption(PathOption::pointer pathOption)
{
	if(pathOption->mCurrencyID==mDstAmount.getCurrency())
	{
		pathOption->mCorrectCurrency=true;

		if(pathOption->mCurrentAccount==mDstAccountID)
		{ // this path is complete
			mCompletePaths.push_back(pathOption);
		}else mBuildingPaths.push_back(pathOption);
	}
	else
	{
		pathOption->mCorrectCurrency=false;
		mBuildingPaths.push_back(pathOption);
	}
}
// vim:ts=4
