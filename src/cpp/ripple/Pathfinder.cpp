
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

		// Expand the current path.
		pspCurrent->setExpanded(lesActive, spPath, mDstAccountID, mSrcAccountID);

		// Determine if expanded current path is the default.
		// When path is a default (implied). Don't need to add it to return set.
		bDefault	= pspCurrent->vpnNodes == mPsDefault->vpnNodes;

		cLog(lsDEBUG) << "findPaths: expanded path: " << pspCurrent->getJson();
		cLog(lsDEBUG) << "findPaths: default path: indirect: " << spPath.getJson(0);

		return bDefault;
	}

	return false;
}

Pathfinder::Pathfinder(const RippleAddress& uSrcAccountID, const RippleAddress& uDstAccountID, const uint160& uSrcCurrencyID, const uint160& uSrcIssuerID, const STAmount& saDstAmount)
	: mSrcAccountID(uSrcAccountID.getAccountID()),
		mDstAccountID(uDstAccountID.getAccountID()),
		mDstAmount(saDstAmount),
		mSrcCurrencyID(uSrcCurrencyID),
		mSrcIssuerID(uSrcIssuerID),
		mOrderBook(theApp->getLedgerMaster().getCurrentLedger())
{
	mLedger		= theApp->getLedgerMaster().getCurrentLedger();
	mSrcAmount	= STAmount(uSrcCurrencyID, uSrcIssuerID, 1, 0, true);	// -1/uSrcIssuerID/uSrcIssuerID

	// Construct the default path for later comparison.

	PathState::pointer	psDefault	= boost::make_shared<PathState>(mDstAmount, mSrcAmount, mLedger);

	if (psDefault)
	{
		// Build the default path.
		// Later, reject anything that expands to the default path as the default is sufficient.

		LedgerEntrySet	lesActive(mLedger);

		psDefault->setExpanded(lesActive, STPath(), mDstAccountID, mSrcAccountID);

		if (tesSUCCESS == psDefault->terStatus)
		{
			// The default path works, remember it.
			cLog(lsDEBUG) << "Pathfinder: default path: " << psDefault->getJson();

			mPsDefault	= psDefault;
		}
		else
		{
			// The default path doesn't work.
			cLog(lsDEBUG) << "Pathfinder: default path: NONE: " << transToken(psDefault->terStatus);
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
		std::queue<STPath>	qspExplore;			// Path stubs to explore.

		STPath				spSeed;
		bool				bForcedIssuer	= !!mSrcCurrencyID && mSrcIssuerID != mSrcAccountID;	// Source forced an issuer.

		// The end is the cursor, start at the source account.
		STPathElement		speEnd(mSrcAccountID,
									mSrcCurrencyID,
									!!mSrcCurrencyID
										? mSrcAccountID	// Non-XRP, start with self as issuer.
										: ACCOUNT_XRP);

		// Build a path of one element: the source.
		spSeed.addElement(speEnd);

		if (bForcedIssuer)
		{
			// Add forced source issuer to seed, via issuer's account.
			STPathElement	speIssuer(mSrcIssuerID, mSrcCurrencyID, mSrcIssuerID);

			spSeed.addElement(speEnd);
		}

		// Push the seed path to explore.
		qspExplore.push(spSeed);

		while (qspExplore.size()) {										// Have paths to explore?
			STPath spPath = qspExplore.front();

			qspExplore.pop();											// Pop the first path from the queue.

			speEnd = spPath.mPath.back();								// Get the last node from the path.

			if (!speEnd.mCurrencyID										// Tail output is XRP.
				&& !mDstAmount.getCurrency())							// Which is dst currency.
			{
				// Done, cursor produces XRP and dest wants XRP.

				// Remove implied source.
				spPath.mPath.erase(spPath.mPath.begin());

				if (bForcedIssuer)
				{
					// Remove implied source issuer.
					spPath.mPath.erase(spPath.mPath.begin());
				}

				if (spPath.size())
				{
					// There is an actual path element.
					cLog(lsDEBUG) << "findPaths: adding path: " << spPath.getJson(0);

					vspResults.push_back(spPath);						// Potential result.
				}
				else
				{
					cLog(lsDEBUG) << "findPaths: empty path: XRP->XRP";
				}

				continue;
			}

			cLog(lsDEBUG) << "findPaths: finish? account: " << (speEnd.mAccountID == mDstAccountID);
			cLog(lsDEBUG) << "findPaths: finish? currency: " << (speEnd.mCurrencyID == mDstAmount.getCurrency());
			cLog(lsDEBUG) << "findPaths: finish? issuer: "
				<< RippleAddress::createHumanAccountID(speEnd.mIssuerID)
				<< " / "
				<< RippleAddress::createHumanAccountID(mDstAmount.getIssuer())
				<< " / "
				<< RippleAddress::createHumanAccountID(mDstAccountID);
			cLog(lsDEBUG) << "findPaths: finish? issuer is desired: " << (speEnd.mIssuerID == mDstAmount.getIssuer());

			// YYY Allows going through self.  Is this wanted?
			if (speEnd.mAccountID == mDstAccountID						// Tail is destination account.
				&& speEnd.mCurrencyID == mDstAmount.getCurrency()		// With correct output currency.
				&& (   speEnd.mIssuerID == mDstAccountID				// Dest always accepts own issuer.
					|| mDstAmount.getIssuer() == mDstAccountID			// Any issuer is good.
					|| mDstAmount.getIssuer() == speEnd.mIssuerID))		// The desired issuer.
			{
				// Done, found a path to the destination.
				// Cursor on the dest account with correct currency and issuer.

				if (bDefaultPath(spPath)) {
					cLog(lsDEBUG) << "findPaths: dropping: default path: " << spPath.getJson(0);

					bFound	= true;
				}
				else
				{
					// Remove implied nodes.

					spPath.mPath.erase(spPath.mPath.begin());

					if (bForcedIssuer)
					{
						// Remove implied source issuer.
						spPath.mPath.erase(spPath.mPath.begin());
					}
					spPath.mPath.erase(spPath.mPath.begin() + spPath.mPath.size()-1);

					vspResults.push_back(spPath);						// Potential result.

					cLog(lsDEBUG) << "findPaths: adding path: " << spPath.getJson(0);
				}

				continue;
			}

			bool	bContinued	= false;								// True, if wasn't a dead end.

			cLog(lsDEBUG) <<
				boost::str(boost::format("findPaths: cursor: %s - %s/%s")
					% RippleAddress::createHumanAccountID(speEnd.mAccountID)
					% STAmount::createHumanCurrency(speEnd.mCurrencyID)
					% RippleAddress::createHumanAccountID(speEnd.mIssuerID));

			if (spPath.mPath.size() == iMaxSteps)
			{
				// Path is at maximum size. Don't want to add more.

				cLog(lsDEBUG)
					<< boost::str(boost::format("findPaths: dropping: path would exceed max steps"));

				continue;
			}
			else if (!speEnd.mCurrencyID)
			{
				// Cursor is for XRP, continue with qualifying books: XRP -> non-XRP

				BOOST_FOREACH(OrderBook::ref book, mOrderBook.getXRPInBooks())
				{
					// New end is an order book with the currency and issuer.

					// Don't allow looping through same order books.
					if (!spPath.hasSeen(ACCOUNT_XRP, book->getCurrencyOut(), book->getIssuerOut()))
					{
						STPath			spNew(spPath);
						STPathElement	speBook(ACCOUNT_XRP, book->getCurrencyOut(), book->getIssuerOut());
						STPathElement	speAccount(book->getIssuerOut(), book->getCurrencyOut(), book->getIssuerOut());

						spNew.mPath.push_back(speBook);		// Add the order book.
						spNew.mPath.push_back(speAccount);	// Add the account and currency

						cLog(lsDEBUG) <<
							boost::str(boost::format("findPaths: XRP -> %s/%s")
								% STAmount::createHumanCurrency(speBook.mCurrencyID)
								% RippleAddress::createHumanAccountID(speBook.mIssuerID));

						qspExplore.push(spNew);

						bContinued	= true;
					}
				}

				tLog(!bContinued, lsDEBUG)
					<< boost::str(boost::format("findPaths: XRP -> dead end"));
			}
			else
			{
				// Last element is for non-XRP, continue by adding ripple lines and order books.

				// Create new paths for each outbound account not already in the path.
				AccountItems	rippleLines(speEnd.mAccountID, mLedger, AccountItem::pointer(new RippleState()));
				SLE::pointer	sleSrc			= lesActive.entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(speEnd.mAccountID));

				tLog(sleSrc, lsDEBUG)
					<< boost::str(boost::format("findPaths: account without root: %s")
						% RippleAddress::createHumanAccountID(speEnd.mAccountID));

				bool			bRequireAuth	= isSetBit(sleSrc->getFieldU32(sfFlags), lsfRequireAuth);

				BOOST_FOREACH(AccountItem::ref item, rippleLines.getItems())
				{
					RippleState*	rspEntry	= (RippleState*) item.get();
					const uint160	uPeerID		= rspEntry->getAccountIDPeer().getAccountID();

					if (spPath.hasSeen(uPeerID, speEnd.mCurrencyID, uPeerID))
					{
						// Peer is in path already. Ignore it to avoid a loop.
						cLog(lsDEBUG) <<
							boost::str(boost::format("findPaths: SEEN: %s/%s -> %s/%s")
								% RippleAddress::createHumanAccountID(speEnd.mAccountID)
								% STAmount::createHumanCurrency(speEnd.mCurrencyID)
								% RippleAddress::createHumanAccountID(uPeerID)
								% STAmount::createHumanCurrency(speEnd.mCurrencyID));
					}
					else if (!rspEntry->getBalance().isPositive()							// No IOUs to send.
						&& (!rspEntry->getLimitPeer()										// Peer does not extend credit.
							|| *rspEntry->getBalance().negate() >= rspEntry->getLimitPeer()	// No credit left.
							|| (bRequireAuth && !rspEntry->getAuth())))						// Not authorized to hold credit.
					{
						// Path has no credit left. Ignore it.
						cLog(lsDEBUG) <<
							boost::str(boost::format("findPaths: No credit: %s/%s -> %s/%s")
								% RippleAddress::createHumanAccountID(speEnd.mAccountID)
								% STAmount::createHumanCurrency(speEnd.mCurrencyID)
								% RippleAddress::createHumanAccountID(uPeerID)
								% STAmount::createHumanCurrency(speEnd.mCurrencyID));
					}
					else
					{
						// Can transmit IOUs and account to the path.
						STPath			spNew(spPath);
						STPathElement	speNew(uPeerID, speEnd.mCurrencyID, uPeerID);

						spNew.mPath.push_back(speNew);
						qspExplore.push(spNew);

						bContinued	= true;

						cLog(lsDEBUG) <<
							boost::str(boost::format("findPaths: push explore: %s/%s -> %s/%s")
								% STAmount::createHumanCurrency(speEnd.mCurrencyID)
								% RippleAddress::createHumanAccountID(speEnd.mAccountID)
								% STAmount::createHumanCurrency(speEnd.mCurrencyID)
								% RippleAddress::createHumanAccountID(uPeerID));
					}
				}

				// Every book that wants the source currency.
				std::vector<OrderBook::pointer> books;

				mOrderBook.getBooks(speEnd.mIssuerID, speEnd.mCurrencyID, books);

				BOOST_FOREACH(OrderBook::ref book, books)
				{
					if (!spPath.hasSeen(ACCOUNT_XRP, book->getCurrencyOut(), book->getIssuerOut()))
					{
						// A book we haven't seen before. Add it.
						STPath			spNew(spPath);
						STPathElement	speBook(ACCOUNT_XRP, book->getCurrencyOut(), book->getIssuerOut());

						spNew.mPath.push_back(speBook);
						qspExplore.push(spNew);

						bContinued	= true;

						cLog(lsDEBUG) <<
							boost::str(boost::format("findPaths: push book: %s/%s -> %s/%s")
								% STAmount::createHumanCurrency(speEnd.mCurrencyID)
								% RippleAddress::createHumanAccountID(speEnd.mIssuerID)
								% STAmount::createHumanCurrency(book->getCurrencyOut())
								% RippleAddress::createHumanAccountID(book->getIssuerOut()));
					}
				}

				tLog(!bContinued, lsDEBUG)
					<< boost::str(boost::format("findPaths: dropping: non-XRP -> dead end"));
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

				TER			terResult;

				try {
					terResult	= RippleCalc::rippleCalc(
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
				}
				catch (const std::exception& e)
				{
					cLog(lsINFO) << "findPaths: Caught throw: " << e.what();

					terResult	= tefEXCEPTION;
				}

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
