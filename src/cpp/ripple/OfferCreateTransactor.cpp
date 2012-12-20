#include "Application.h"

#include "OfferCreateTransactor.h"

#include <boost/foreach.hpp>
#include <boost/bind.hpp>

SETUP_LOG();

// Take as much as possible. Adjusts account balances. Charges fees on top to taker.
// -->   uBookBase: The order book to take against.
// --> saTakerPays: What the taker offers (w/ issuer)
// --> saTakerGets: What the taker wanted (w/ issuer)
// <-- saTakerPaid: What taker paid not including fees. To reduce an offer.
// <--  saTakerGot: What taker got not including fees. To reduce an offer.
// <--   terResult: tesSUCCESS or terNO_ACCOUNT
TER OfferCreateTransactor::takeOffers(
	bool				bPassive,
	const uint256&		uBookBase,
	const uint160&		uTakerAccountID,
	const SLE::pointer&	sleTakerAccount,
	const STAmount&		saTakerPays,
	const STAmount&		saTakerGets,
	STAmount&			saTakerPaid,
	STAmount&			saTakerGot)
{
	assert(saTakerPays && saTakerGets);

	cLog(lsINFO) << "takeOffers: against book: " << uBookBase.ToString();

	uint256					uTipIndex			= uBookBase;
	const uint256			uBookEnd			= Ledger::getQualityNext(uBookBase);
	const uint64			uTakeQuality		= STAmount::getRate(saTakerGets, saTakerPays);
	const uint160			uTakerPaysAccountID	= saTakerPays.getIssuer();
	const uint160			uTakerGetsAccountID	= saTakerGets.getIssuer();
	TER						terResult			= temUNCERTAIN;

	boost::unordered_set<uint256>	usOfferUnfundedFound;	// Offers found unfunded.
	boost::unordered_set<uint256>	usOfferUnfundedBecame;	// Offers that became unfunded.
	boost::unordered_set<uint160>	usAccountTouched;		// Accounts touched.

	saTakerPaid	= STAmount(saTakerPays.getCurrency(), saTakerPays.getIssuer());
	saTakerGot	= STAmount(saTakerGets.getCurrency(), saTakerGets.getIssuer());

	while (temUNCERTAIN == terResult)
	{
		SLE::pointer	sleOfferDir;
		uint64			uTipQuality;

		// Figure out next offer to take, if needed.
		if (saTakerGets != saTakerGot && saTakerPays != saTakerPaid)
		{
			// Taker, still, needs to get and pay.

			sleOfferDir		= mEngine->entryCache(ltDIR_NODE, mEngine->getLedger()->getNextLedgerIndex(uTipIndex, uBookEnd));
			if (sleOfferDir)
			{
				cLog(lsINFO) << "takeOffers: possible counter offer found";

				uTipIndex		= sleOfferDir->getIndex();
				uTipQuality		= Ledger::getQuality(uTipIndex);
			}
			else
			{
				cLog(lsINFO) << "takeOffers: counter offer book is empty: "
					<< uTipIndex.ToString()
					<< " ... "
					<< uBookEnd.ToString();
			}
		}

		if (!sleOfferDir									// No offer directory to take.
			|| uTakeQuality < uTipQuality					// No offers of sufficient quality available.
			|| (bPassive && uTakeQuality == uTipQuality))
		{
			// Done.
			cLog(lsINFO) << "takeOffers: done";

			terResult	= tesSUCCESS;
		}
		else
		{
			// Have an offer directory to consider.
			cLog(lsINFO) << "takeOffers: considering dir: " << sleOfferDir->getJson(0);

			SLE::pointer	sleBookNode;
			unsigned int	uBookEntry;
			uint256			uOfferIndex;

			mEngine->getNodes().dirFirst(uTipIndex, sleBookNode, uBookEntry, uOfferIndex);

			SLE::pointer	sleOffer		= mEngine->entryCache(ltOFFER, uOfferIndex);

			cLog(lsINFO) << "takeOffers: considering offer : " << sleOffer->getJson(0);

			const uint160	uOfferOwnerID	= sleOffer->getFieldAccount(sfAccount).getAccountID();
			STAmount		saOfferPays		= sleOffer->getFieldAmount(sfTakerGets);
			STAmount		saOfferGets		= sleOffer->getFieldAmount(sfTakerPays);

			if (sleOffer->isFieldPresent(sfExpiration) && sleOffer->getFieldU32(sfExpiration) <= mEngine->getLedger()->getParentCloseTimeNC())
			{
				// Offer is expired. Expired offers are considered unfunded. Delete it.
				cLog(lsINFO) << "takeOffers: encountered expired offer";

				usOfferUnfundedFound.insert(uOfferIndex);
			}
			else if (uOfferOwnerID == uTakerAccountID)
			{
				// Would take own offer. Consider old offer expired. Delete it.
				cLog(lsINFO) << "takeOffers: encountered taker's own old offer";

				usOfferUnfundedFound.insert(uOfferIndex);
			}
			else
			{
				// Get offer funds available.

				cLog(lsINFO) << "takeOffers: saOfferPays=" << saOfferPays.getFullText();

				STAmount		saOfferFunds	= mEngine->getNodes().accountFunds(uOfferOwnerID, saOfferPays, true);
				STAmount		saTakerFunds	= mEngine->getNodes().accountFunds(uTakerAccountID, saTakerPays, true);
				SLE::pointer	sleOfferAccount;	// Owner of offer.

				if (!saOfferFunds.isPositive())
				{
					// Offer is unfunded, possibly due to previous balance action.
					cLog(lsINFO) << "takeOffers: offer unfunded: delete";

					boost::unordered_set<uint160>::iterator	account	= usAccountTouched.find(uOfferOwnerID);
					if (account != usAccountTouched.end())
					{
						// Previously touched account.
						usOfferUnfundedBecame.insert(uOfferIndex);	// Delete unfunded offer on success.
					}
					else
					{
						// Never touched source account.
						usOfferUnfundedFound.insert(uOfferIndex);	// Delete found unfunded offer when possible.
					}
				}
				else
				{
					STAmount	saPay		= saTakerPays - saTakerPaid;
					if (saTakerFunds < saPay)
						saPay	= saTakerFunds;
					STAmount	saSubTakerPaid;
					STAmount	saSubTakerGot;
					STAmount	saTakerIssuerFee;
					STAmount	saOfferIssuerFee;

					cLog(lsINFO) << "takeOffers: applyOffer:    saTakerPays: " << saTakerPays.getFullText();
					cLog(lsINFO) << "takeOffers: applyOffer:    saTakerPaid: " << saTakerPaid.getFullText();
					cLog(lsINFO) << "takeOffers: applyOffer:   saTakerFunds: " << saTakerFunds.getFullText();
					cLog(lsINFO) << "takeOffers: applyOffer:   saOfferFunds: " << saOfferFunds.getFullText();
					cLog(lsINFO) << "takeOffers: applyOffer:          saPay: " << saPay.getFullText();
					cLog(lsINFO) << "takeOffers: applyOffer:    saOfferPays: " << saOfferPays.getFullText();
					cLog(lsINFO) << "takeOffers: applyOffer:    saOfferGets: " << saOfferGets.getFullText();
					cLog(lsINFO) << "takeOffers: applyOffer:    saTakerPays: " << saTakerPays.getFullText();
					cLog(lsINFO) << "takeOffers: applyOffer:    saTakerGets: " << saTakerGets.getFullText();

					bool	bOfferDelete	= STAmount::applyOffer(
						mEngine->getNodes().rippleTransferRate(uTakerAccountID, uOfferOwnerID, uTakerPaysAccountID),
						mEngine->getNodes().rippleTransferRate(uOfferOwnerID, uTakerAccountID, uTakerGetsAccountID),
						saOfferFunds,
						saPay,				// Driver XXX need to account for fees.
						saOfferPays,
						saOfferGets,
						saTakerPays,
						saTakerGets,
						saSubTakerPaid,
						saSubTakerGot,
						saTakerIssuerFee,
						saOfferIssuerFee);

					cLog(lsINFO) << "takeOffers: applyOffer: saSubTakerPaid: " << saSubTakerPaid.getFullText();
					cLog(lsINFO) << "takeOffers: applyOffer:  saSubTakerGot: " << saSubTakerGot.getFullText();

					// Adjust offer

					// Offer owner will pay less.  Subtract what taker just got.
					sleOffer->setFieldAmount(sfTakerGets, saOfferPays -= saSubTakerGot);

					// Offer owner will get less.  Subtract what owner just paid.
					sleOffer->setFieldAmount(sfTakerPays, saOfferGets -= saSubTakerPaid);

					mEngine->entryModify(sleOffer);

					if (bOfferDelete)
					{
						// Offer now fully claimed or now unfunded.
						cLog(lsINFO) << "takeOffers: offer claimed: delete";

						usOfferUnfundedBecame.insert(uOfferIndex);	// Delete unfunded offer on success.

						// Offer owner's account is no longer pristine.
						usAccountTouched.insert(uOfferOwnerID);
					}
					else
					{
						cLog(lsINFO) << "takeOffers: offer partial claim.";
					}

					// Offer owner pays taker.
					// saSubTakerGot.setIssuer(uTakerGetsAccountID);	// XXX Move this earlier?
					assert(!!saSubTakerGot.getIssuer());

					mEngine->getNodes().accountSend(uOfferOwnerID, uTakerAccountID, saSubTakerGot);
					mEngine->getNodes().accountSend(uOfferOwnerID, uTakerGetsAccountID, saOfferIssuerFee);

					saTakerGot	+= saSubTakerGot;

					// Taker pays offer owner.
					//	saSubTakerPaid.setIssuer(uTakerPaysAccountID);
					assert(!!saSubTakerPaid.getIssuer());

					mEngine->getNodes().accountSend(uTakerAccountID, uOfferOwnerID, saSubTakerPaid);
					mEngine->getNodes().accountSend(uTakerAccountID, uTakerPaysAccountID, saTakerIssuerFee);

					saTakerPaid	+= saSubTakerPaid;
				}
			}
		}
	}

	// On storing meta data, delete offers that were found unfunded to prevent encountering them in future.
	if (tesSUCCESS == terResult)
	{
		BOOST_FOREACH(const uint256& uOfferIndex, usOfferUnfundedFound)
		{
			terResult	= mEngine->getNodes().offerDelete(uOfferIndex);
			if (tesSUCCESS != terResult)
				break;
		}
	}

	if (tesSUCCESS == terResult)
	{
		// On success, delete offers that became unfunded.
		BOOST_FOREACH(const uint256& uOfferIndex, usOfferUnfundedBecame)
		{
			terResult	= mEngine->getNodes().offerDelete(uOfferIndex);
			if (tesSUCCESS != terResult)
				break;
		}
	}

	return terResult;
}

TER OfferCreateTransactor::doApply()
{
	cLog(lsWARNING) << "doOfferCreate> " << mTxn.getJson(0);
	const uint32			uTxFlags		= mTxn.getFlags();
	const bool				bPassive		= isSetBit(uTxFlags, tfPassive);
	STAmount				saTakerPays		= mTxn.getFieldAmount(sfTakerPays);
	STAmount				saTakerGets		= mTxn.getFieldAmount(sfTakerGets);

	cLog(lsINFO) << boost::str(boost::format("doOfferCreate: saTakerPays=%s saTakerGets=%s")
		% saTakerPays.getFullText()
		% saTakerGets.getFullText());

	const uint160			uPaysIssuerID	= saTakerPays.getIssuer();
	const uint160			uGetsIssuerID	= saTakerGets.getIssuer();
	const uint32			uExpiration		= mTxn.getFieldU32(sfExpiration);
	const bool				bHaveExpiration	= mTxn.isFieldPresent(sfExpiration);
	const uint32			uSequence		= mTxn.getSequence();

	const uint256			uLedgerIndex	= Ledger::getOfferIndex(mTxnAccountID, uSequence);

	cLog(lsINFO) << "doOfferCreate: Creating offer node: " << uLedgerIndex.ToString() << " uSequence=" << uSequence;

	const uint160			uPaysCurrency	= saTakerPays.getCurrency();
	const uint160			uGetsCurrency	= saTakerGets.getCurrency();
	const uint64			uRate			= STAmount::getRate(saTakerGets, saTakerPays);

	TER						terResult		= tesSUCCESS;
	uint256					uDirectory;		// Delete hints.
	uint64					uOwnerNode;
	uint64					uBookNode;

	if (uTxFlags & tfOfferCreateMask)
	{
		cLog(lsINFO) << "doOfferCreate: Malformed transaction: Invalid flags set.";

		return temINVALID_FLAG;
	}
	else if (bHaveExpiration && !uExpiration)
	{
		cLog(lsWARNING) << "doOfferCreate: Malformed offer: bad expiration";

		terResult	= temBAD_EXPIRATION;
	}
	else if (bHaveExpiration && mEngine->getLedger()->getParentCloseTimeNC() >= uExpiration)
	{
		cLog(lsWARNING) << "doOfferCreate: Expired transaction: offer expired";

		terResult	= tesSUCCESS;				// Only charged fee.
	}
	else if (saTakerPays.isNative() && saTakerGets.isNative())
	{
		cLog(lsWARNING) << "doOfferCreate: Malformed offer: XRP for XRP";

		terResult	= temBAD_OFFER;
	}
	else if (!saTakerPays.isPositive() || !saTakerGets.isPositive())
	{
		cLog(lsWARNING) << "doOfferCreate: Malformed offer: bad amount";

		terResult	= temBAD_OFFER;
	}
	else if (uPaysCurrency == uGetsCurrency && uPaysIssuerID == uGetsIssuerID)
	{
		cLog(lsWARNING) << "doOfferCreate: Malformed offer: redundant offer";

		terResult	= temREDUNDANT;
	}
	else if (saTakerPays.isNative() != !uPaysIssuerID || saTakerGets.isNative() != !uGetsIssuerID)
	{
		cLog(lsWARNING) << "doOfferCreate: Malformed offer: bad issuer";

		terResult	= temBAD_ISSUER;
	}
	else if (!mEngine->getNodes().accountFunds(mTxnAccountID, saTakerGets, true).isPositive())
	{
		cLog(lsWARNING) << "doOfferCreate: delay: Offers must be at least partially funded.";

		terResult	= terUNFUNDED;
	}

	if (tesSUCCESS == terResult && !saTakerPays.isNative())
	{
		SLE::pointer		sleTakerPays	= mEngine->entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uPaysIssuerID));

		if (!sleTakerPays)
		{
			cLog(lsWARNING) << "doOfferCreate: delay: can't receive IOUs from non-existent issuer: " << RippleAddress::createHumanAccountID(uPaysIssuerID);

			terResult	= terNO_ACCOUNT;
		}
	}

	STAmount		saOfferPaid;
	STAmount		saOfferGot;

	if (tesSUCCESS == terResult)
	{
		const uint256	uTakeBookBase	= Ledger::getBookBase(uGetsCurrency, uGetsIssuerID, uPaysCurrency, uPaysIssuerID);

		cLog(lsINFO) << boost::str(boost::format("doOfferCreate: take against book: %s for %s -> %s")
			% uTakeBookBase.ToString()
			% saTakerGets.getFullText()
			% saTakerPays.getFullText());

		// Take using the parameters of the offer.
		cLog(lsWARNING) << "doOfferCreate: takeOffers: BEFORE saTakerGets=" << saTakerGets.getFullText();
		terResult	= takeOffers(
			bPassive,
			uTakeBookBase,
			mTxnAccountID,
			mTxnAccount,
			saTakerGets,
			saTakerPays,
			saOfferPaid,	// How much was spent.
			saOfferGot		// How much was got.
			);

		cLog(lsWARNING) << "doOfferCreate: takeOffers=" << terResult;
		cLog(lsWARNING) << "doOfferCreate: takeOffers: saOfferPaid=" << saOfferPaid.getFullText();
		cLog(lsWARNING) << "doOfferCreate: takeOffers:  saOfferGot=" << saOfferGot.getFullText();
		cLog(lsWARNING) << "doOfferCreate: takeOffers: saTakerPays=" << saTakerPays.getFullText();
		cLog(lsWARNING) << "doOfferCreate: takeOffers: AFTER saTakerGets=" << saTakerGets.getFullText();

		if (tesSUCCESS == terResult)
		{
			saTakerPays		-= saOfferGot;				// Reduce payin from takers by what offer just got.
			saTakerGets		-= saOfferPaid;				// Reduce payout to takers by what srcAccount just paid.
		}
	}

	cLog(lsWARNING) << "doOfferCreate: takeOffers: saTakerPays=" << saTakerPays.getFullText();
	cLog(lsWARNING) << "doOfferCreate: takeOffers: saTakerGets=" << saTakerGets.getFullText();
	cLog(lsWARNING) << "doOfferCreate: takeOffers: mTxnAccountID=" << RippleAddress::createHumanAccountID(mTxnAccountID);
	cLog(lsWARNING) << "doOfferCreate: takeOffers:         FUNDS=" << mEngine->getNodes().accountFunds(mTxnAccountID, saTakerGets, true).getFullText();

	// cLog(lsWARNING) << "doOfferCreate: takeOffers: uPaysIssuerID=" << RippleAddress::createHumanAccountID(uPaysIssuerID);
	// cLog(lsWARNING) << "doOfferCreate: takeOffers: uGetsIssuerID=" << RippleAddress::createHumanAccountID(uGetsIssuerID);

	if (tesSUCCESS != terResult
		|| !saTakerPays														// Wants nothing more.
		|| !saTakerGets														// Offering nothing more.
		|| !mEngine->getNodes().accountFunds(mTxnAccountID, saTakerGets, true).isPositive())	// Not funded.
	{
		// Complete as is.
		nothing();
	}
	else if (mTxnAccount->getFieldAmount(sfBalance).getNValue() < mEngine->getLedger()->getReserve(mTxnAccount->getFieldU32(sfOwnerCount)+1))
	{
		if (isSetBit(mParams, tapOPEN_LEDGER)) // Ledger is not final, can vote no.
		{
			// Hope for more reserve to come in or more offers to consume.
			terResult	= terINSUF_RESERVE_OFFER;
		}
		else if (!saOfferPaid && !saOfferGot)
		{
			// Ledger is final, insufficent reserve to create offer, processed nothing.

			terResult	= tepINSUF_RESERVE_OFFER;
		}
		else
		{
			// Ledger is final, insufficent reserve to create offer, processed something.

			// Consider the offer unfunded. Treat as tesSUCCESS.
			nothing();
		}
	}
	else
	{
		// We need to place the remainder of the offer into its order book.
		cLog(lsINFO) << boost::str(boost::format("doOfferCreate: offer not fully consumed: saTakerPays=%s saTakerGets=%s")
			% saTakerPays.getFullText()
			% saTakerGets.getFullText());

		// Add offer to owner's directory.
		terResult	= mEngine->getNodes().dirAdd(uOwnerNode, Ledger::getOwnerDirIndex(mTxnAccountID), uLedgerIndex,
			boost::bind(&Ledger::qualityDirDescriber, _1, saTakerPays.getCurrency(), uPaysIssuerID,
				saTakerGets.getCurrency(), uGetsIssuerID, uRate));


		if (tesSUCCESS == terResult)
		{
			mEngine->getNodes().ownerCountAdjust(mTxnAccountID, 1, mTxnAccount); // Update owner count.

			uint256	uBookBase	= Ledger::getBookBase(uPaysCurrency, uPaysIssuerID, uGetsCurrency, uGetsIssuerID);

			cLog(lsINFO) << boost::str(boost::format("doOfferCreate: adding to book: %s : %s/%s -> %s/%s")
				% uBookBase.ToString()
				% saTakerPays.getHumanCurrency()
				% RippleAddress::createHumanAccountID(saTakerPays.getIssuer())
				% saTakerGets.getHumanCurrency()
				% RippleAddress::createHumanAccountID(saTakerGets.getIssuer()));

			uDirectory	= Ledger::getQualityIndex(uBookBase, uRate);	// Use original rate.

			// Add offer to order book.
			terResult	= mEngine->getNodes().dirAdd(uBookNode, uDirectory, uLedgerIndex,
				boost::bind(&Ledger::qualityDirDescriber, _1, saTakerPays.getCurrency(), uPaysIssuerID,
					saTakerGets.getCurrency(), uGetsIssuerID, uRate));
		}

		if (tesSUCCESS == terResult)
		{
			cLog(lsWARNING) << "doOfferCreate: sfAccount=" << RippleAddress::createHumanAccountID(mTxnAccountID);
			cLog(lsWARNING) << "doOfferCreate: uPaysIssuerID=" << RippleAddress::createHumanAccountID(uPaysIssuerID);
			cLog(lsWARNING) << "doOfferCreate: uGetsIssuerID=" << RippleAddress::createHumanAccountID(uGetsIssuerID);
			cLog(lsWARNING) << "doOfferCreate: saTakerPays.isNative()=" << saTakerPays.isNative();
			cLog(lsWARNING) << "doOfferCreate: saTakerGets.isNative()=" << saTakerGets.isNative();
			cLog(lsWARNING) << "doOfferCreate: uPaysCurrency=" << saTakerPays.getHumanCurrency();
			cLog(lsWARNING) << "doOfferCreate: uGetsCurrency=" << saTakerGets.getHumanCurrency();

			SLE::pointer			sleOffer		= mEngine->entryCreate(ltOFFER, uLedgerIndex);

			sleOffer->setFieldAccount(sfAccount, mTxnAccountID);
			sleOffer->setFieldU32(sfSequence, uSequence);
			sleOffer->setFieldH256(sfBookDirectory, uDirectory);
			sleOffer->setFieldAmount(sfTakerPays, saTakerPays);
			sleOffer->setFieldAmount(sfTakerGets, saTakerGets);
			sleOffer->setFieldU64(sfOwnerNode, uOwnerNode);
			sleOffer->setFieldU64(sfBookNode, uBookNode);

			if (uExpiration)
				sleOffer->setFieldU32(sfExpiration, uExpiration);

			if (bPassive)
				sleOffer->setFlag(lsfPassive);

			cLog(lsINFO) << boost::str(boost::format("doOfferCreate: final terResult=%s sleOffer=%s")
				% transToken(terResult)
				% sleOffer->getJson(0));
		}
	}

	tLog(tesSUCCESS != terResult, lsINFO) << boost::str(boost::format("doOfferCreate: final terResult=%s") % transToken(terResult));

	return terResult;
}

// vim:ts=4
