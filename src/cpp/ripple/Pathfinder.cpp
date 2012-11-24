#include "Pathfinder.h"
#include "Application.h"
#include "RippleLines.h"
#include "Log.h"
#include <boost/foreach.hpp>

SETUP_LOG();

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
	getXRPOffers();

	// return list of all orderbooks that want XRP
	// return list of all orderbooks that want IssuerID
	// return list of all orderbooks that want this issuerID and currencyID
*/

/*
Test sending to XRP
Test XRP to XRP
Test offer in middle
Test XRP to USD
Test USD to EUR
*/

// we sort the options by:
//    cost of path
//    length of path
//    width of path
//    correct currency at the end
#if 0
bool sortPathOptions(PathOption::pointer first, PathOption::pointer second)
{
	if (first->mTotalCost<second->mTotalCost) return(true);
	if (first->mTotalCost>second->mTotalCost) return(false);

	if (first->mCorrectCurrency && !second->mCorrectCurrency) return(true);
	if (!first->mCorrectCurrency && second->mCorrectCurrency) return(false);

	if (first->mPath.size()<second->mPath.size()) return(true);
	if (first->mPath.size()>second->mPath.size()) return(false);

	if (first->mMinWidth<second->mMinWidth) return true;

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
#endif

//
// XXX Optionally, specifying a source and destination issuer might be nice. Especially, to convert between issuers. However, this
// functionality is left to the future.
//
Pathfinder::Pathfinder(const RippleAddress& srcAccountID, const RippleAddress& dstAccountID, const uint160& srcCurrencyID, const uint160& srcIssuerID, const STAmount& dstAmount)
	: mSrcAccountID(srcAccountID.getAccountID()), mDstAccountID(dstAccountID.getAccountID()), mDstAmount(dstAmount), mSrcCurrencyID(srcCurrencyID), mSrcIssuerID(srcIssuerID), mOrderBook(theApp->getLedgerMaster().getCurrentLedger())
{
	mLedger=theApp->getLedgerMaster().getCurrentLedger();
}

// If possible, returns a single path.
// --> maxSearchSteps: unused XXX
// --> maxPay: unused XXX
// When generating a path set blindly, don't allow the empty path, it is implied by default.
// When generating a path set for estimates, allow an empty path instead of no paths to indicate a path exists. The caller will
// need to strip the empty path when submitting the transaction.
bool Pathfinder::findPaths(int maxSearchSteps, int maxPay, STPathSet& retPathSet, bool bAllowEmpty)
{
	if (mLedger) {
		std::queue<STPath> pqueue;
		STPathElement ele(mSrcAccountID,
				  mSrcCurrencyID,
				  uint160());	// XXX Might add source issuer.
		STPath path;

		path.addElement(ele);	// Add the source.

		pqueue.push(path);

		while (pqueue.size()) {
			STPath path = pqueue.front();

			pqueue.pop();				// Pop the first path from the queue.

			ele = path.mPath.back();	// Get the last node from the path.

			// Done, if dest wants XRP and last element produces XRP.
			if (!ele.mCurrencyID									// Tail output is XRP
				&& !mDstAmount.getCurrency()) {

				// Remove implied first.
				path.mPath.erase(path.mPath.begin());

				// Return the path.
				retPathSet.addPath(path);

				cLog(lsDEBUG) << "findPaths: adding: " << path.getJson(0);

				return true;
			}

			// Done, if dest wants non-XRP and last element is dest.
			// YYY Allows going through self.  Is this wanted?
			if (ele.mAccountID == mDstAccountID						// Tail is destination
				&& ele.mCurrencyID == mDstAmount.getCurrency()) {	// With correct output currency.
				// Found a path to the destination.

				if (!bAllowEmpty && 2 == path.mPath.size()) {
					// Empty path is default. Drop it.
					cLog(lsDEBUG) << "findPaths: dropping empty path.";
					continue;
				}

				// Remove implied first and last nodes.
				path.mPath.erase(path.mPath.begin());
				path.mPath.erase(path.mPath.begin() + path.mPath.size()-1);

				// Return the path.
				retPathSet.addPath(path);

				cLog(lsDEBUG) << "findPaths: adding: " << path.getJson(0);

				return true;
			}

			bool	bContinued	= false;

			if (!ele.mCurrencyID) {
				// Last element is for XRP continue with qualifying books.
				BOOST_FOREACH(OrderBook::pointer book, mOrderBook.getXRPInBooks())
				{
					// XXX Don't allow looping through same order books.

					//if (!path.hasSeen(line->getAccountIDPeer().getAccountID()))
					{
						STPath			new_path(path);
						STPathElement	new_ele(uint160(), book->getCurrencyOut(), book->getIssuerOut());

						new_path.mPath.push_back(new_ele);
						new_path.mCurrencyID		= book->getCurrencyOut();
						new_path.mCurrentAccount	= book->getCurrencyOut();

						cLog(lsDEBUG) <<
							boost::str(boost::format("findPaths: XRP input - %s/%s")
								% STAmount::createHumanCurrency(new_path.mCurrencyID)
								% RippleAddress::createHumanAccountID(new_path.mCurrentAccount));

						pqueue.push(new_path);

						bContinued	= true;
					}
				}

				tLog(!bContinued, lsDEBUG)
					<< boost::str(boost::format("findPaths: XRP input - dead end"));

			} else {
				// Last element is for non-XRP continue by adding ripple lines and order books.

				// Create new paths for each outbound account not already in the path.
				RippleLines rippleLines(ele.mAccountID);

				BOOST_FOREACH(RippleState::pointer line, rippleLines.getLines())
				{
					if (!path.hasSeen(line->getAccountIDPeer().getAccountID()))
					{
						STPath			new_path(path);
						STPathElement	new_ele(line->getAccountIDPeer().getAccountID(),
											ele.mCurrencyID,
											uint160());

						cLog(lsDEBUG) <<
							boost::str(boost::format("findPaths: %s/%s --> %s/%s")
								% RippleAddress::createHumanAccountID(ele.mAccountID)
								% STAmount::createHumanCurrency(ele.mCurrencyID)
								% RippleAddress::createHumanAccountID(line->getAccountIDPeer().getAccountID())
								% STAmount::createHumanCurrency(ele.mCurrencyID));

						new_path.mPath.push_back(new_ele);
						pqueue.push(new_path);

						bContinued	= true;
					}
				}

				// Every book that wants the source currency.
				std::vector<OrderBook::pointer> books;

				mOrderBook.getBooks(path.mCurrentAccount, path.mCurrencyID, books);

				BOOST_FOREACH(OrderBook::pointer book,books)
				{
					STPath			new_path(path);
					STPathElement	new_ele(uint160(), book->getCurrencyOut(), book->getIssuerOut());

					cLog(lsDEBUG) <<
						boost::str(boost::format("findPaths: %s/%s :: %s/%s")
							% STAmount::createHumanCurrency(ele.mCurrencyID)
							% RippleAddress::createHumanAccountID(ele.mAccountID)
							% STAmount::createHumanCurrency(book->getCurrencyOut())
							% RippleAddress::createHumanAccountID(book->getIssuerOut()));

					new_path.mPath.push_back(new_ele);
					new_path.mCurrentAccount=book->getIssuerOut();
					new_path.mCurrencyID=book->getCurrencyOut();

					pqueue.push(new_path);

					bContinued	= true;
				}
			}

			tLog(!bContinued, lsDEBUG)
				<< boost::str(boost::format("findPaths: non-XRP input - dead end"));
		}
	}
	else
	{
		cLog(lsWARNING) << boost::str(boost::format("findPaths: no ledger"));
	}

	return false;
}

#if 0
bool Pathfinder::checkComplete(STPathSet& retPathSet)
{
	if (mCompletePaths.size())
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
//   if source is XRP
//		every offer that wants XRP
//   else
//		every ripple line that starts with the source currency
//		every offer that we can take that wants the source currency

void Pathfinder::addOptions(PathOption::pointer tail)
{
	if (!tail->mCurrencyID)
	{ // source XRP
		BOOST_FOREACH(OrderBook::pointer book, mOrderBook.getXRPInBooks())
		{
			PathOption::pointer pathOption(new PathOption(tail));

			STPathElement ele(uint160(), book->getCurrencyOut(), book->getIssuerOut());
			pathOption->mPath.addElement(ele);

			pathOption->mCurrentAccount=book->getIssuerOut();
			pathOption->mCurrencyID=book->getCurrencyOut();
			addPathOption(pathOption);
		}
	}
	else
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
#endif

// vim:ts=4
