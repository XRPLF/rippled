
#include "Pathfinder.h"

#include <queue>

#include <boost/foreach.hpp>

#include "Application.h"
#include "Log.h"

SETUP_LOG();

/*
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

// quality, length, liquidity, index
typedef boost::tuple<uint64, int, STAmount, unsigned int> path_LQ_t;

// Lower numbers have better quality. Sort higher quality first.
static bool bQualityCmp(const path_LQ_t& a, const path_LQ_t&b)
{
	// 1) Higher quality (lower cost) is better
	if (a.get<0>() != b.get<0>())
		return a.get<0>() < b.get<0>();

	// 2) More liquidity (higher volume) is better
	if (a.get<2>() != b.get<2>())
		return a.get<2>() > b.get<2>();

	// 3) Shorter paths are better
	if (a.get<1>() != b.get<1>())
		return a.get<1>() < b.get<1>();

	// 4) Tie breaker
	return a.get<3>() > b.get<3>();
}

// Return true, if path is a default path with an element.
// A path is a default path if it is implied via src, dst, send, and sendmax.
bool Pathfinder::bDefaultPath(const STPath& spPath)
{
	if (2 >= spPath.mPath.size()) {
		// Empty path is a default. Don't need to add it to return set.
		cLog(lsTRACE) << "findPaths: empty path: direct";

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
		LedgerEntrySet	lesActive(mLedger, tapNONE);

		cLog(lsTRACE) << boost::str(boost::format("bDefaultPath> mSrcAmount=%s mDstAmount=%s")
				% mSrcAmount.getFullText()
				% mDstAmount.getFullText());

		// Expand the current path.
		pspCurrent->setExpanded(lesActive, spPath, mDstAccountID, mSrcAccountID);
		// XXX Need to report or act on errors returned in pspCurrent->terStatus.

		// Determine if expanded current path is the default.
		// When path is a default (implied). Don't need to add it to return set.
		bDefault	= pspCurrent->vpnNodes == mPsDefault->vpnNodes;

		cLog(lsTRACE) << "bDefaultPath: expanded path: " << pspCurrent->getJson();
		cLog(lsTRACE) << "bDefaultPath: source path: " << spPath.getJson(0);
		cLog(lsTRACE) << "bDefaultPath: default path: " << mPsDefault->getJson();

		return bDefault;
	}

	return false;
}

typedef std::pair<int, uint160> candidate_t;
bool candCmp(uint32 seq, const candidate_t& first, const candidate_t& second)
{
	if (first.first < second.first)
		return false;
	if (first.first > second.first)
		return true;
	return (first.first ^ seq) < (second.first ^ seq);
}

Pathfinder::Pathfinder(Ledger::ref ledger,
		const RippleAddress& uSrcAccountID, const RippleAddress& uDstAccountID,
		const uint160& uSrcCurrencyID, const uint160& uSrcIssuerID, const STAmount& saDstAmount, bool bValid)
	:	mSrcAccountID(uSrcAccountID.getAccountID()),
		mDstAccountID(uDstAccountID.getAccountID()),
		mDstAmount(saDstAmount),
		mSrcCurrencyID(uSrcCurrencyID),
		mSrcIssuerID(uSrcIssuerID),
		mSrcAmount(uSrcCurrencyID, uSrcIssuerID, 1u, 0, true),
		mLedger(ledger)
{

	if ((mSrcAccountID == mDstAccountID) && (mSrcCurrencyID == mDstAmount.getCurrency()))
	{ // no need to send to same account with same currency
		bValid = false;
		return;
	}
	bValid = true;

	theApp->getOrderBookDB().setup(mLedger);

	mLoadMonitor = theApp->getJobQueue().getLoadEvent(jtPATH_FIND, "FindPath");

	// Construct the default path for later comparison.

	PathState::pointer	psDefault	= boost::make_shared<PathState>(mDstAmount, mSrcAmount, mLedger);

	if (psDefault)
	{
		// Build the default path.
		// Later, reject anything that expands to the default path as the default is sufficient.

		LedgerEntrySet	lesActive(mLedger, tapNONE);

		cLog(lsTRACE) << boost::str(boost::format("Pathfinder> mSrcAmount=%s mDstAmount=%s")
				% mSrcAmount.getFullText()
				% mDstAmount.getFullText());

		psDefault->setExpanded(lesActive, STPath(), mDstAccountID, mSrcAccountID);

		if (tesSUCCESS == psDefault->terStatus)
		{
			// The default path works, remember it.
			cLog(lsTRACE) << "Pathfinder: default path: " << psDefault->getJson();

			mPsDefault	= psDefault;
		}
		else
		{
			// The default path doesn't work.
			cLog(lsTRACE) << "Pathfinder: default path: NONE: " << transToken(psDefault->terStatus);
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

	cLog(lsTRACE) << boost::str(boost::format("findPaths> mSrcAccountID=%s mDstAccountID=%s mDstAmount=%s mSrcCurrencyID=%s mSrcIssuerID=%s")
		% RippleAddress::createHumanAccountID(mSrcAccountID)
		% RippleAddress::createHumanAccountID(mDstAccountID)
		% mDstAmount.getFullText()
		% STAmount::createHumanCurrency(mSrcCurrencyID)
		% RippleAddress::createHumanAccountID(mSrcIssuerID)
		);

	if (!mLedger)
	{
		cLog(lsDEBUG) << "findPaths< no ledger";

		return false;
	}

	LedgerEntrySet		lesActive(mLedger, tapNONE);
	boost::unordered_map<uint160, AccountItems::pointer> aiMap;

	SLE::pointer	sleSrc		= lesActive.entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(mSrcAccountID));
	if (!sleSrc)
	{
		cLog(lsDEBUG) << boost::str(boost::format("findPaths< no source"));

		return false;
	}

	SLE::pointer	sleDst		= lesActive.entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(mDstAccountID));
	if (!sleDst)
	{
		cLog(lsDEBUG) << boost::str(boost::format("findPaths< no dest"));

		return false;
	}

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
				cLog(lsTRACE) << "findPaths: adding path: " << spPath.getJson(0);

				vspResults.push_back(spPath);						// Potential result.
			}
			else
			{
				cLog(lsWARNING) << "findPaths: empty path: XRP->XRP";
			}

			continue;
		}

		if (sLog(lsTRACE))
		{
			cLog(lsTRACE) << boost::str(boost::format("findPaths: spe: %s/%s: %s amt: %s")
				% RippleAddress::createHumanAccountID(speEnd.mAccountID)
				% RippleAddress::createHumanAccountID(speEnd.mIssuerID)
				% RippleAddress::createHumanAccountID(mDstAccountID)
				% RippleAddress::createHumanAccountID(mDstAmount.getIssuer()));

			cLog(lsTRACE) << "findPaths: finish? account: " << (speEnd.mAccountID == mDstAccountID);
			cLog(lsTRACE) << "findPaths: finish? currency: " << (speEnd.mCurrencyID == mDstAmount.getCurrency());
			cLog(lsTRACE) << "findPaths: finish? issuer: "
				<< RippleAddress::createHumanAccountID(speEnd.mIssuerID)
				<< " / "
				<< RippleAddress::createHumanAccountID(mDstAmount.getIssuer())
				<< " / "
				<< RippleAddress::createHumanAccountID(mDstAccountID);
			cLog(lsTRACE) << "findPaths: finish? issuer is desired: " << (speEnd.mIssuerID == mDstAmount.getIssuer());
		}

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
				cLog(lsTRACE) << "findPaths: dropping: default path: " << spPath.getJson(0);

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

		cLog(lsTRACE) <<
			boost::str(boost::format("findPaths: cursor: %s - %s/%s")
				% RippleAddress::createHumanAccountID(speEnd.mAccountID)
				% STAmount::createHumanCurrency(speEnd.mCurrencyID)
				% RippleAddress::createHumanAccountID(speEnd.mIssuerID));

		if (spPath.mPath.size() >= iMaxSteps)
		{
			// Path is at maximum size. Don't want to add more.

			cLog(lsTRACE)
				<< boost::str(boost::format("findPaths: dropping: path would exceed max steps"));

			continue;
		}
		else if (!speEnd.mCurrencyID)
		{
			// XXX Might restrict the number of times bridging through XRP.

			// Cursor is for XRP, continue with qualifying books: XRP -> non-XRP
			std::vector<OrderBook::pointer> xrpBooks;
			theApp->getOrderBookDB().getBooksByTakerPays(ACCOUNT_XRP, CURRENCY_XRP, xrpBooks);
			BOOST_FOREACH(OrderBook::ref book, xrpBooks)
			{
				// New end is an order book with the currency and issuer.

				if (!spPath.hasSeen(ACCOUNT_XRP, book->getCurrencyOut(), book->getIssuerOut()) &&
					!matchesOrigin(book->getCurrencyOut(), book->getIssuerOut()))
				{
					// Not a order book already in path.
					STPath			spNew(spPath);
					STPathElement	speBook(ACCOUNT_XRP, book->getCurrencyOut(), book->getIssuerOut());
					STPathElement	speAccount(book->getIssuerOut(), book->getCurrencyOut(), book->getIssuerOut());

					spNew.mPath.push_back(speBook);		// Add the order book.
					spNew.mPath.push_back(speAccount);	// Add the account and currency

					cLog(lsDEBUG)
						<< boost::str(boost::format("findPaths: XRP -> %s/%s")
//							% STAmount::createHumanCurrency(book->getCurrencyOut())
//							% RippleAddress::createHumanAccountID(book->getIssuerOut())
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

			SLE::pointer	sleEnd			= lesActive.entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(speEnd.mAccountID));

			tLog(!sleEnd, lsDEBUG)
				<< boost::str(boost::format("findPaths: tail: %s/%s : ")
					% RippleAddress::createHumanAccountID(speEnd.mAccountID)
					% RippleAddress::createHumanAccountID(speEnd.mIssuerID));

			if (sleEnd)
			{
				// On a non-XRP account:
				// True, the cursor requires the next node to be authorized.
				bool			bRequireAuth	= isSetBit(sleEnd->getFieldU32(sfFlags), lsfRequireAuth);
				bool			dstCurrency		= speEnd.mCurrencyID == mDstAmount.getCurrency();

				AccountItems& rippleLines(getRippleLines(speEnd.mAccountID));

				std::vector< std::pair<int, uint160> > candidates;
				candidates.reserve(rippleLines.getItems().size());

				BOOST_FOREACH(AccountItem::ref item, rippleLines.getItems())
				{
					RippleState*	rspEntry	= (RippleState*) item.get();
					const uint160&	uPeerID		= rspEntry->getAccountIDPeer();

					if (speEnd.mCurrencyID != rspEntry->getLimit().getCurrency())
					{
						// wrong currency
						nothing();
					}
					else if (spPath.hasSeen(uPeerID, speEnd.mCurrencyID, uPeerID) ||
						((uPeerID == mSrcAccountID) && (uPeerID != mDstAccountID)))
					{
						// Peer is in path already. Ignore it to avoid a loop.
						cLog(lsTRACE) <<
							boost::str(boost::format("findPaths: SEEN: %s/%s -> %s/%s")
								% RippleAddress::createHumanAccountID(speEnd.mAccountID)
								% STAmount::createHumanCurrency(speEnd.mCurrencyID)
								% RippleAddress::createHumanAccountID(uPeerID)
								% STAmount::createHumanCurrency(speEnd.mCurrencyID));
					}
					else if (!rspEntry->getBalance().isPositive()							// No IOUs to send.
						&& (!rspEntry->getLimitPeer()										// Peer does not extend credit.
							|| -rspEntry->getBalance() >= rspEntry->getLimitPeer()			// No credit left.
							|| (bRequireAuth && !rspEntry->getAuth())))						// Not authorized to hold credit.
					{
						// Path has no credit left. Ignore it.
						cLog(lsTRACE) <<
							boost::str(boost::format("findPaths: No credit: %s/%s -> %s/%s balance=%s limit=%s")
								% RippleAddress::createHumanAccountID(speEnd.mAccountID)
								% STAmount::createHumanCurrency(speEnd.mCurrencyID)
								% RippleAddress::createHumanAccountID(uPeerID)
								% STAmount::createHumanCurrency(speEnd.mCurrencyID)
								% rspEntry->getBalance().getFullText()
								% rspEntry->getLimitPeer().getFullText()
								);
					}
					else if (dstCurrency && (uPeerID == mDstAccountID))
					{ // never skip the destination node
						candidates.push_back(std::make_pair(1000000, uPeerID));
					}
					else
					{ // save this candidate
						int out = getPathsOut(speEnd.mCurrencyID, uPeerID, bRequireAuth, dstCurrency, mDstAccountID);
						if (out != 0)
							candidates.push_back(std::make_pair(out, uPeerID));
					}
				}

				if (!candidates.empty())
				{
					bFound = true;

					std::sort(candidates.begin(), candidates.end(),
						BIND_TYPE(candCmp, mLedger->getLedgerSeq(), P_1, P_2));
					int count = candidates.size();

					if (count > 30)
						count /= 3;
					else if (count > 10)
						count /= 2;

					std::vector< std::pair<int, uint160> >::iterator it = candidates.begin();
					while (count-- != 0)
					{
						STPath			spNew(spPath);
						STPathElement	speNew(it->second, speEnd.mCurrencyID, it->second);

						spNew.mPath.push_back(speNew);
						qspExplore.push(spNew);

						bContinued	= true;

						cLog(lsTRACE) <<
							boost::str(boost::format("findPaths: push explore: %s/%s -> %s/%s")
								% STAmount::createHumanCurrency(speEnd.mCurrencyID)
								% RippleAddress::createHumanAccountID(speEnd.mAccountID)
								% STAmount::createHumanCurrency(speEnd.mCurrencyID)
								% RippleAddress::createHumanAccountID(it->second));
						++it;
					}
				}
			}


			// XXX Flip argument order to norm. (currency, issuer)
			std::vector<OrderBook::pointer> books;
			theApp->getOrderBookDB().getBooksByTakerPays(speEnd.mIssuerID, speEnd.mCurrencyID, books);

			BOOST_FOREACH(OrderBook::ref book, books)
			{
				if (!spPath.hasSeen(ACCOUNT_XRP, book->getCurrencyOut(), book->getIssuerOut()) &&
					!matchesOrigin(book->getCurrencyOut(), book->getIssuerOut()))
				{
					// A book we haven't seen before. Add it.
					STPath			spNew(spPath);
					STPathElement	speBook(ACCOUNT_XRP, book->getCurrencyOut(), book->getIssuerOut(),
						book->getCurrencyIn() != book->getCurrencyOut());

					spNew.mPath.push_back(speBook);		// Add the order book.

					if (!!book->getCurrencyOut())
					{
						// For non-XRP out, don't end on the book, add the issuing account.
						STPathElement	speAccount(book->getIssuerOut(), book->getCurrencyOut(), book->getIssuerOut());
						spNew.mPath.push_back(speAccount);	// Add the account and currency
					}

					qspExplore.push(spNew);

					bContinued	= true;

					cLog(lsTRACE) <<
						boost::str(boost::format("findPaths: push book: %s/%s -> %s/%s")
							% STAmount::createHumanCurrency(speEnd.mCurrencyID)
							% RippleAddress::createHumanAccountID(speEnd.mIssuerID)
							% STAmount::createHumanCurrency(book->getCurrencyOut())
							% RippleAddress::createHumanAccountID(book->getIssuerOut()));
				}
			}

			tLog(!bContinued, lsTRACE)
				<< boost::str(boost::format("findPaths: dropping: non-XRP -> dead end"));
		}
	}

	unsigned int iLimit  = std::min(iMaxPaths, (unsigned int) vspResults.size());

	// Only filter, sort, and limit if have non-default paths.
	if (iLimit)
	{
		std::vector<path_LQ_t> vMap;

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
				LedgerEntrySet lesSandbox(lesActive.duplicate());

				terResult	= RippleCalc::rippleCalc(
					lesSandbox,
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

				vMap.push_back(path_LQ_t(uQuality, spCurrent.mPath.size(), saDstAmountAct, i));
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
			std::sort(vMap.begin(), vMap.end(), bQualityCmp);	// Lower is better and should be first.

			STAmount remaining = mDstAmount;
			if (bFound)
			{ // must subtract liquidity in default path from remaining amount
				try
				{
					STAmount saMaxAmountAct, saDstAmountAct;
					std::vector<PathState::pointer> vpsExpanded;
					LedgerEntrySet lesSandbox(lesActive.duplicate());

					TER result = RippleCalc::rippleCalc(
							lesSandbox,
							saMaxAmountAct,
							saDstAmountAct,
							vpsExpanded,
							mSrcAmount,
							mDstAmount,
							mDstAccountID,
							mSrcAccountID,
							STPathSet(),
							true,		// allow partial payment
							false,
							false,		// don't suppress default paths, that's the point
							true);

					if (tesSUCCESS == result)
					{
						cLog(lsDEBUG) << "Default path contributes: " << saDstAmountAct;
						remaining -= saDstAmountAct;
					}
					else
					{
						cLog(lsDEBUG) << "Default path fails: " << transToken(result);
					}
				}
				catch (...)
				{
					cLog(lsDEBUG) << "Default path causes exception";
				}
			}

			for (int i = 0, iPathsLeft = iMaxPaths; (iPathsLeft > 0) && (i < vMap.size()); ++i)
			{
				path_LQ_t& lqt = vMap[i];
				if ((iPathsLeft != 1) || (lqt.get<2>() >= remaining))
				{ // last path must fill
					--iPathsLeft;
					remaining -= lqt.get<2>();
					spsDst.addPath(vspResults[lqt.get<3>()]);
				}
				else
					cLog(lsDEBUG) << "Skipping a non-filling path: " << vspResults[lqt.get<3>()].getJson(0);
			}

			if (remaining.isPositive())
			{
				bFound = false;
				cLog(lsINFO) << "Paths could not send " << remaining << " of " << mDstAmount;
			}
			else
				bFound = true;

			cLog(lsDEBUG) << boost::str(boost::format("findPaths: RESULTS: %s") % spsDst.getJson(0));
		}
		else
		{
			cLog(lsDEBUG) << boost::str(boost::format("findPaths: RESULTS: non-defaults filtered away"));
		}
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
		std::vector<OrderBook::pointer> xrpBooks;
		theApp->getOrderBookDB().getBooksByTakerPays(ISSUER_XRP, CURRENCY_XRP, xrpBooks);
		BOOST_FOREACH(OrderBook::ref book, xrpBooks)
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

		// Every offer that can take this currency in
		std::vector<OrderBook::pointer> books;
		theApp->getOrderBookDB().getBooksByTakerPays(tail->mCurrentAccount, tail->mCurrencyID, books);
		BOOST_FOREACH(OrderBook::ref book, books)
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
		if (saBalance.isPositive()								// Have IOUs to send.
			|| (rspEntry->getLimitPeer()						// Peer extends credit.
				&& ((-saBalance) < rspEntry->getLimitPeer())))	// Credit left.
		{
			usCurrencies.insert(saBalance.getCurrency());
		}
	}

	return usCurrencies;
}

AccountItems& Pathfinder::getRippleLines(const uint160& accountID)
{
	boost::unordered_map<uint160, AccountItems::pointer>::iterator it = mRLMap.find(accountID);
	if (it == mRLMap.end())
		it = mRLMap.insert(std::make_pair(accountID, boost::make_shared<AccountItems>
			(boost::cref(accountID), boost::cref(mLedger), AccountItem::pointer(new RippleState())))).first;
	return *it->second;
}

bool Pathfinder::matchesOrigin(const uint160& currency, const uint160& issuer)
{
	return (currency == mSrcCurrencyID) && (issuer == mSrcIssuerID);
}

int Pathfinder::getPathsOut(const uint160& currencyID, const uint160& accountID,
	bool authRequired, bool isDstCurrency, const uint160& dstAccount)
{
	std::pair<const uint160&, const uint160&> accountCurrency(currencyID, accountID);
	boost::unordered_map<std::pair<uint160, uint160>, int>::iterator it = mPOMap.find(accountCurrency);
	if (it != mPOMap.end())
		return it->second;

	int count = 0;
	AccountItems& rippleLines(getRippleLines(accountID));
	BOOST_FOREACH(AccountItem::ref item, rippleLines.getItems())
	{
		RippleState* rspEntry = (RippleState*) item.get();
		if (currencyID != rspEntry->getLimit().getCurrency())
			nothing();
		else if (!rspEntry->getBalance().isPositive() &&
			(!rspEntry->getLimitPeer()
				|| -rspEntry->getBalance() >= rspEntry->getLimitPeer()
				||	(authRequired && !rspEntry->getAuth())))
			nothing();
		else if (isDstCurrency && (dstAccount == rspEntry->getAccountIDPeer()))
			count += 100; // count a path to the destination extra
		else
			++count;
	}
	mPOMap[accountCurrency] = count;
	return count;
}

// vim:ts=4
