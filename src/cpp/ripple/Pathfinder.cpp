
#include "Pathfinder.h"

#include <queue>

#include <boost/foreach.hpp>

#include "Application.h"
#include "AccountItems.h"
#include "Log.h"

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

// Lower numbers have better quality. Sort higher quality first.
static bool bQualityCmp(std::pair<uint32, unsigned int> a, std::pair<uint32, unsigned int> b)
{
	return a.first < b.first;
}

// Return true, if path is a default path with an element.
// A path is a default path if it is implied via src, dst, send, and sendmax.
bool Pathfinder::bDefaultPath(const STPath& spPath)
{
	if (2 == spPath.mPath.size()) {
		// Empty path is a default. Don't need to add it to return set.
		cLog(lsDEBUG) << "findPaths: empty path: direct";

		return true;
	}

	if (!mPsDefault)
	{
		// No default path.
		// There might not be a direct credit line or there may be no implied nodes
		// in send and sendmax.

		return false;			// Didn't generate a default path. So can't match.
	}

	PathState::pointer	pspCurrent	= boost::make_shared<PathState>(mDstAmount, mSrcAmount, mLedger);

	if (pspCurrent)
	{
		bool			bDefault;
		LedgerEntrySet	lesActive(mLedger);

		pspCurrent->setExpanded(lesActive, spPath, mDstAccountID, mSrcAccountID);

		bDefault	= pspCurrent->vpnNodes == mPsDefault->vpnNodes;

		cLog(lsDEBUG) << "findPaths: expanded path: " << pspCurrent->getJson();

		// Path is a default (implied). Don't need to add it to return set.
		cLog(lsDEBUG) << "findPaths: default path: indirect: " << spPath.getJson(0);

		return bDefault;
	}

	return false;
}

//
// XXX Optionally, specifying a source and destination issuer might be nice. Especially, to convert between issuers. However, this
// functionality is left to the future.
//
Pathfinder::Pathfinder(const RippleAddress& uSrcAccountID, const RippleAddress& uDstAccountID, const uint160& uSrcCurrencyID, const uint160& uSrcIssuerID, const STAmount& saDstAmount)
	: mSrcAccountID(uSrcAccountID.getAccountID()), mDstAccountID(uDstAccountID.getAccountID()), mDstAmount(saDstAmount), mSrcCurrencyID(uSrcCurrencyID), mSrcIssuerID(uSrcIssuerID), mOrderBook(theApp->getLedgerMaster().getCurrentLedger())
{
	mLedger		= theApp->getLedgerMaster().getCurrentLedger();
	mSrcAmount	= STAmount(uSrcCurrencyID, uSrcIssuerID, 1, 0, true);	// -1/uSrcIssuerID/uSrcIssuerID

	// Construct the default path for later comparison.

	PathState::pointer	psDefault	= boost::make_shared<PathState>(mDstAmount, mSrcAmount, mLedger);

	if (psDefault)
	{
		LedgerEntrySet	lesActive(mLedger);

		psDefault->setExpanded(lesActive, STPath(), mDstAccountID, mSrcAccountID);

		if (tesSUCCESS == psDefault->terStatus)
		{
			cLog(lsDEBUG) << "Pathfinder: reference path: " << psDefault->getJson();
			mPsDefault	= psDefault;
		}
		else
		{
			cLog(lsDEBUG) << "Pathfinder: reference path: NONE: " << transToken(psDefault->terStatus);
		}
	}
}

// If possible, returns a single path.
// --> iMaxSteps: Maximum nodes in paths to return.
// --> iMaxPaths: Maximum number of paths to return.
// <-- retPathSet: founds paths not including default paths.
// Returns true if found paths.
//
// When generating a path set blindly, don't allow the empty path, it is implied by default.
// When generating a path set for estimates, allow an empty path instead of no paths to indicate a path exists. The caller will
// need to strip the empty path when submitting the transaction.
//
// Assumes rippling (not XRP to XRP)
//
// Leaves to the caller figuring out overall liquidity.
// Optimization opportunity: For some simple cases, this routine has figured out the overall liquidity.
bool Pathfinder::findPaths(const unsigned int iMaxSteps, const unsigned int iMaxPaths, STPathSet& spsDst)
{
	bool	bFound		= false;	// True, iff found a path.

	cLog(lsDEBUG) << boost::str(boost::format("findPaths> mSrcAccountID=%s mDstAccountID=%s mDstAmount=%s mSrcCurrencyID=%s mSrcIssuerID=%s")
		% RippleAddress::createHumanAccountID(mSrcAccountID)
		% RippleAddress::createHumanAccountID(mDstAccountID)
		% mDstAmount.getFullText()
		% STAmount::createHumanCurrency(mSrcCurrencyID)
		% RippleAddress::createHumanAccountID(mSrcIssuerID)
		);

	if (mLedger)
	{
		LedgerEntrySet		lesActive(mLedger);
		std::vector<STPath>	vspResults;
		std::queue<STPath>	qspExplore;

		STPathElement		speEnd(mSrcAccountID,
								  mSrcCurrencyID,
								  uint160());	// XXX Might add source issuer.
		STPath path;

		path.addElement(speEnd);				// Add the source.

		qspExplore.push(path);

		while (qspExplore.size()) {
			STPath spPath = qspExplore.front();

			qspExplore.pop();											// Pop the first path from the queue.

			speEnd = spPath.mPath.back();								// Get the last node from the path.

			// Done, if dest wants XRP and last element produces XRP.
			if (!speEnd.mCurrencyID										// Tail output is XRP.
				&& !mDstAmount.getCurrency())							// Which is dst currency.
			{
				// Remove implied first.
				spPath.mPath.erase(spPath.mPath.begin());

				if (spPath.size())
				{
					// There is an actual path element.
					cLog(lsDEBUG) << "findPaths: adding: " << spPath.getJson(0);

					vspResults.push_back(spPath);						// Potential result.
				}
				else
				{
					cLog(lsDEBUG) << "findPaths: empty path: XRP->XRP";
				}

				continue;
			}

			// Done, if dest wants non-XRP and last element is dest.
			// YYY Allows going through self.  Is this wanted?
			if (speEnd.mAccountID == mDstAccountID						// Tail is destination account.
				&& speEnd.mCurrencyID == mDstAmount.getCurrency())		// With correct output currency.
			{
				// Found a path to the destination.

				if (bDefaultPath(spPath)) {
					cLog(lsDEBUG) << "findPaths: default path: dropping: " << spPath.getJson(0);

					bFound	= true;
				}
				else
				{

					// Remove implied first and last nodes.
					spPath.mPath.erase(spPath.mPath.begin());
					spPath.mPath.erase(spPath.mPath.begin() + spPath.mPath.size()-1);

					vspResults.push_back(spPath);						// Potential result.

					cLog(lsDEBUG) << "findPaths: adding: " << spPath.getJson(0);
				}

				continue;
			}

			bool	bContinued	= false;

			if (spPath.mPath.size() == iMaxSteps)
			{
				// Path is at maximum size. Don't want to add more.

				cLog(lsDEBUG)
					<< boost::str(boost::format("findPaths: path would exceed max steps: dropping"));
			}
			else if (!speEnd.mCurrencyID)
			{
				// Last element is for XRP, continue with qualifying books.
				BOOST_FOREACH(OrderBook::ref book, mOrderBook.getXRPInBooks())
				{
					// XXX Don't allow looping through same order books.

					//if (!path.hasSeen(line->getAccountIDPeer().getAccountID()))
					{
						STPath			new_path(spPath);
						STPathElement	new_ele(uint160(), book->getCurrencyOut(), book->getIssuerOut());

						new_path.mPath.push_back(new_ele);
						new_path.mCurrencyID		= book->getCurrencyOut();
						new_path.mCurrentAccount	= book->getCurrencyOut();

						cLog(lsDEBUG) <<
							boost::str(boost::format("findPaths: XRP input - %s/%s")
								% STAmount::createHumanCurrency(new_path.mCurrencyID)
								% RippleAddress::createHumanAccountID(new_path.mCurrentAccount));

						qspExplore.push(new_path);

						bContinued	= true;
					}
				}

				tLog(!bContinued, lsDEBUG)
					<< boost::str(boost::format("findPaths: XRP input - dead end"));
			}
			else
			{
				// Last element is for non-XRP, continue by adding ripple lines and order books.

				// Create new paths for each outbound account not already in the path.
				AccountItems rippleLines(speEnd.mAccountID, mLedger, AccountItem::pointer(new RippleState()));

				BOOST_FOREACH(AccountItem::ref item, rippleLines.getItems())
				{
					RippleState* rspEntry = (RippleState*) item.get();

					if (spPath.hasSeen(rspEntry->getAccountIDPeer().getAccountID()))
					{
						// Peer is in path already. Ignore it to avoid a loop.
						cLog(lsDEBUG) <<
							boost::str(boost::format("findPaths: SEEN: %s/%s --> %s/%s")
								% RippleAddress::createHumanAccountID(speEnd.mAccountID)
								% STAmount::createHumanCurrency(speEnd.mCurrencyID)
								% RippleAddress::createHumanAccountID(rspEntry->getAccountIDPeer().getAccountID())
								% STAmount::createHumanCurrency(speEnd.mCurrencyID));
					}
					else if (!rspEntry->getBalance().isPositive()	// No IOUs to send.
						&& (!rspEntry->getLimitPeer()				// Peer does not extend credit.
							|| *rspEntry->getBalance().negate() >= rspEntry->getLimitPeer()))	// No credit left.
					{
						// Path has no credit left. Ignore it.
						cLog(lsDEBUG) <<
							boost::str(boost::format("findPaths: No credit: %s/%s --> %s/%s")
								% RippleAddress::createHumanAccountID(speEnd.mAccountID)
								% STAmount::createHumanCurrency(speEnd.mCurrencyID)
								% RippleAddress::createHumanAccountID(rspEntry->getAccountIDPeer().getAccountID())
								% STAmount::createHumanCurrency(speEnd.mCurrencyID));
					}
					else
					{
						// Can transmit IOUs.
						STPath			new_path(spPath);
						STPathElement	new_ele(rspEntry->getAccountIDPeer().getAccountID(),
											speEnd.mCurrencyID,
											uint160());

						cLog(lsDEBUG) <<
							boost::str(boost::format("findPaths: %s/%s --> %s/%s")
								% RippleAddress::createHumanAccountID(speEnd.mAccountID)
								% STAmount::createHumanCurrency(speEnd.mCurrencyID)
								% RippleAddress::createHumanAccountID(rspEntry->getAccountIDPeer().getAccountID())
								% STAmount::createHumanCurrency(speEnd.mCurrencyID));

						new_path.mPath.push_back(new_ele);
						qspExplore.push(new_path);

						bContinued	= true;
					}
				}

				// Every book that wants the source currency.
				std::vector<OrderBook::pointer> books;

				mOrderBook.getBooks(spPath.mCurrentAccount, spPath.mCurrencyID, books);

				BOOST_FOREACH(OrderBook::ref book,books)
				{
					STPath			new_path(spPath);
					STPathElement	new_ele(uint160(), book->getCurrencyOut(), book->getIssuerOut());

					cLog(lsDEBUG) <<
						boost::str(boost::format("findPaths: %s/%s :: %s/%s")
							% STAmount::createHumanCurrency(speEnd.mCurrencyID)
							% RippleAddress::createHumanAccountID(speEnd.mAccountID)
							% STAmount::createHumanCurrency(book->getCurrencyOut())
							% RippleAddress::createHumanAccountID(book->getIssuerOut()));

					new_path.mPath.push_back(new_ele);
					new_path.mCurrentAccount=book->getIssuerOut();
					new_path.mCurrencyID=book->getCurrencyOut();

					qspExplore.push(new_path);

					bContinued	= true;
				}

				tLog(!bContinued, lsDEBUG)
					<< boost::str(boost::format("findPaths: non-XRP input - dead end"));
			}
		}

		unsigned int iLimit  = std::min(iMaxPaths, (unsigned int) vspResults.size());

		// Only filter, sort, and limit if have non-default paths.
		if (iLimit)
		{
			std::vector< std::pair<uint64, unsigned int> > vMap;

			// Build map of quality to entry.
			for (int i = vspResults.size(); i--;)
			{
				STAmount	saMaxAmountAct;
				STAmount	saDstAmountAct;
				std::vector<PathState::pointer>	vpsExpanded;
				STPathSet	spsPaths;
				STPath&		spCurrent	= vspResults[i];

				spsPaths.addPath(spCurrent);				// Just checking the current path.

				TER			terResult	= RippleCalc::rippleCalc(
					lesActive,
					saMaxAmountAct,
					saDstAmountAct,
					vpsExpanded,
					mSrcAmount,			// --> amount to send max.
					mDstAmount,			// --> amount to deliver.
					mDstAccountID,
					mSrcAccountID,
					spsPaths,
					true,				// --> bPartialPayment: Allow, it might contribute.
					false,				// --> bLimitQuality: Assume normal transaction.
					true,				// --> bNoRippleDirect: Providing the only path.
					true);				// --> bStandAlone: Don't need to delete unfundeds.

				if (tesSUCCESS == terResult)
				{
					uint64	uQuality	= STAmount::getRate(saDstAmountAct, saMaxAmountAct);

					cLog(lsDEBUG)
						<< boost::str(boost::format("findPaths: quality: %d: %s")
							% uQuality
							% spCurrent.getJson(0));

					vMap.push_back(std::make_pair(uQuality, i));
				}
				else
				{
					cLog(lsDEBUG)
						<< boost::str(boost::format("findPaths: dropping: %s: %s")
							% transToken(terResult)
							% spCurrent.getJson(0));
				}
			}

			if (vMap.size())
			{
				iLimit	= std::min(iMaxPaths, (unsigned int) vMap.size());

				bFound	= true;

				std::sort(vMap.begin(), vMap.end(), bQualityCmp);	// Lower is better and should be first.

				// Output best quality entries.
				for (int i = 0; i != vMap.size(); ++i)
				{
					spsDst.addPath(vspResults[vMap[i].second]);
				}

				cLog(lsDEBUG) << boost::str(boost::format("findPaths: RESULTS: %s") % spsDst.getJson(0));
			}
			else
			{
				cLog(lsDEBUG) << boost::str(boost::format("findPaths: RESULTS: non-defaults filtered away"));
			}
		}
	}
	else
	{
		cLog(lsDEBUG) << boost::str(boost::format("findPaths: no ledger"));
	}

	cLog(lsDEBUG) << boost::str(boost::format("findPaths< bFound=%d") % bFound);

	return bFound;
}

#if 0
bool Pathfinder::checkComplete(STPathSet& retPathSet)
{
	if (mCompletePaths.size())
	{ // TODO: look through these and pick the most promising
		int count=0;
		BOOST_FOREACH(PathOption::ref pathOption,mCompletePaths)
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
		BOOST_FOREACH(OrderBook::ref book, mOrderBook.getXRPInBooks())
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
		BOOST_FOREACH(RippleState::ref line,rippleLines.getLines())
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

		BOOST_FOREACH(OrderBook::ref book,books)
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

boost::unordered_set<uint160> usAccountSourceCurrencies(const RippleAddress& raAccountID, Ledger::ref lrLedger)
{
	boost::unordered_set<uint160>	usCurrencies;

	// List of ripple lines.
	AccountItems rippleLines(raAccountID.getAccountID(), lrLedger, AccountItem::pointer(new RippleState()));

	BOOST_FOREACH(AccountItem::ref item, rippleLines.getItems())
	{
		RippleState*	rspEntry	= (RippleState*) item.get();
		STAmount		saBalance	= rspEntry->getBalance();

		// Filter out non
		if (saBalance.isPositive()					// Have IOUs to send.
			|| (rspEntry->getLimitPeer()			// Peer extends credit.
				&& *saBalance.negate() < rspEntry->getLimitPeer()))	// Credit left.
		{
			// Path has no credit left. Ignore it.
			usCurrencies.insert(saBalance.getCurrency());
		}
	}

	return usCurrencies;
}
// vim:ts=4
