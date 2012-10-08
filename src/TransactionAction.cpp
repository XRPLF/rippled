//
// XXX Make sure all fields are recognized in transactions.
//

#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include <queue>

#include "TransactionEngine.h"

#include "../json/writer.h"

#include "Config.h"
#include "Contract.h"
#include "Interpreter.h"
#include "Log.h"
#include "RippleCalc.h"
#include "TransactionFormats.h"
#include "utils.h"

#define RIPPLE_PATHS_MAX	3

// Set the authorized public key for an account.  May also set the generator map.
TER	TransactionEngine::setAuthorized(const SerializedTransaction& txn, bool bMustSetGenerator)
{
	//
	// Verify that submitter knows the private key for the generator.
	// Otherwise, people could deny access to generators.
	//

	std::vector<unsigned char>	vucCipher		= txn.getFieldVL(sfGenerator);
	std::vector<unsigned char>	vucPubKey		= txn.getFieldVL(sfPublicKey);
	std::vector<unsigned char>	vucSignature	= txn.getFieldVL(sfSignature);
	NewcoinAddress				naAccountPublic	= NewcoinAddress::createAccountPublic(vucPubKey);

	if (!naAccountPublic.accountPublicVerify(Serializer::getSHA512Half(vucCipher), vucSignature))
	{
		Log(lsWARNING) << "createGenerator: bad signature unauthorized generator claim";

		return tefBAD_GEN_AUTH;
	}

	// Create generator.
	uint160				hGeneratorID	= naAccountPublic.getAccountID();

	SLE::pointer		sleGen			= entryCache(ltGENERATOR_MAP, Ledger::getGeneratorIndex(hGeneratorID));
	if (!sleGen)
	{
		// Create the generator.
		Log(lsTRACE) << "createGenerator: creating generator";

		sleGen			= entryCreate(ltGENERATOR_MAP, Ledger::getGeneratorIndex(hGeneratorID));

		sleGen->setFieldVL(sfGenerator, vucCipher);
	}
	else if (bMustSetGenerator)
	{
		// Doing a claim.  Must set generator.
		// Generator is already in use.  Regular passphrases limited to one wallet.
		Log(lsWARNING) << "createGenerator: generator already in use";

		return tefGEN_IN_USE;
	}

	// Set the public key needed to use the account.
	uint160				uAuthKeyID		= bMustSetGenerator
											? hGeneratorID								// Claim
											: txn.getFieldAccount160(sfAuthorizedKey);	// PasswordSet

	mTxnAccount->setFieldAccount(sfAuthorizedKey, uAuthKeyID);

	return tesSUCCESS;
}

TER TransactionEngine::doAccountSet(const SerializedTransaction& txn)
{
	Log(lsINFO) << "doAccountSet>";

	//
	// EmailHash
	//

	if (txn.isFieldPresent(sfEmailHash))
	{
		uint128		uHash	= txn.getFieldH128(sfEmailHash);

		if (!uHash)
		{
			Log(lsINFO) << "doAccountSet: unset email hash";

			mTxnAccount->makeFieldAbsent(sfEmailHash);
		}
		else
		{
			Log(lsINFO) << "doAccountSet: set email hash";

			mTxnAccount->setFieldH128(sfEmailHash, uHash);
		}
	}

	//
	// WalletLocator
	//

	if (txn.isFieldPresent(sfWalletLocator))
	{
		uint256		uHash	= txn.getFieldH256(sfWalletLocator);

		if (!uHash)
		{
			Log(lsINFO) << "doAccountSet: unset wallet locator";

			mTxnAccount->makeFieldAbsent(sfEmailHash);
		}
		else
		{
			Log(lsINFO) << "doAccountSet: set wallet locator";

			mTxnAccount->setFieldH256(sfWalletLocator, uHash);
		}
	}

	//
	// MessageKey
	//

	if (!txn.isFieldPresent(sfMessageKey))
	{
		nothing();
	}
	else
	{
		Log(lsINFO) << "doAccountSet: set message key";

		mTxnAccount->setFieldVL(sfMessageKey, txn.getFieldVL(sfMessageKey));
	}

	//
	// Domain
	//

	if (txn.isFieldPresent(sfDomain))
	{
		std::vector<unsigned char>	vucDomain	= txn.getFieldVL(sfDomain);

		if (vucDomain.empty())
		{
			Log(lsINFO) << "doAccountSet: unset domain";

			mTxnAccount->makeFieldAbsent(sfDomain);
		}
		else
		{
			Log(lsINFO) << "doAccountSet: set domain";

			mTxnAccount->setFieldVL(sfDomain, vucDomain);
		}
	}

	//
	// TransferRate
	//

	if (txn.isFieldPresent(sfTransferRate))
	{
		uint32		uRate	= txn.getFieldU32(sfTransferRate);

		if (!uRate || uRate == QUALITY_ONE)
		{
			Log(lsINFO) << "doAccountSet: unset transfer rate";

			mTxnAccount->makeFieldAbsent(sfTransferRate);
		}
		else if (uRate > QUALITY_ONE)
		{
			Log(lsINFO) << "doAccountSet: set transfer rate";

			mTxnAccount->setFieldU32(sfTransferRate, uRate);
		}
		else
		{
			Log(lsINFO) << "doAccountSet: bad transfer rate";

			return temBAD_TRANSFER_RATE;
		}
	}

	//
	// PublishHash && PublishSize
	//

	bool	bPublishHash	= txn.isFieldPresent(sfPublishHash);
	bool	bPublishSize	= txn.isFieldPresent(sfPublishSize);

	if (bPublishHash ^ bPublishSize)
	{
		Log(lsINFO) << "doAccountSet: bad publish";

		return temBAD_PUBLISH;
	}
	else if (bPublishHash && bPublishSize)
	{
		uint256		uHash	= txn.getFieldH256(sfPublishHash);
		uint32		uSize	= txn.getFieldU32(sfPublishSize);

		if (!uHash)
		{
			Log(lsINFO) << "doAccountSet: unset publish";

			mTxnAccount->makeFieldAbsent(sfPublishHash);
			mTxnAccount->makeFieldAbsent(sfPublishSize);
		}
		else
		{
			Log(lsINFO) << "doAccountSet: set publish";

			mTxnAccount->setFieldH256(sfPublishHash, uHash);
			mTxnAccount->setFieldU32(sfPublishSize, uSize);
		}
	}

	Log(lsINFO) << "doAccountSet<";

	return tesSUCCESS;
}

TER TransactionEngine::doClaim(const SerializedTransaction& txn)
{
	Log(lsINFO) << "doClaim>";

	TER	terResult	= setAuthorized(txn, true);

	Log(lsINFO) << "doClaim<";

	return terResult;
}

TER TransactionEngine::doCreditSet(const SerializedTransaction& txn)
{
	TER			terResult		= tesSUCCESS;
	Log(lsINFO) << "doCreditSet>";

	const STAmount		saLimitAmount	= txn.getFieldAmount(sfLimitAmount);
	const bool			bQualityIn		= txn.isFieldPresent(sfQualityIn);
	const uint32		uQualityIn		= bQualityIn ? txn.getFieldU32(sfQualityIn) : 0;
	const bool			bQualityOut		= txn.isFieldPresent(sfQualityOut);
	const uint32		uQualityOut		= bQualityIn ? txn.getFieldU32(sfQualityOut) : 0;
	const uint160		uCurrencyID		= saLimitAmount.getCurrency();
	uint160				uDstAccountID	= saLimitAmount.getIssuer();
	const bool			bFlipped		= mTxnAccountID > uDstAccountID;		// true, iff current is not lowest.
	bool				bDelIndex		= false;

	// Check if destination makes sense.

	if (!uDstAccountID)
	{
		Log(lsINFO) << "doCreditSet: Malformed transaction: Destination account not specifed.";

		return temDST_NEEDED;
	}
	else if (mTxnAccountID == uDstAccountID)
	{
		Log(lsINFO) << "doCreditSet: Malformed transaction: Can not extend credit to self.";

		return temDST_IS_SRC;
	}

	SLE::pointer		sleDst		= entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));
	if (!sleDst)
	{
		Log(lsINFO) << "doCreditSet: Delay transaction: Destination account does not exist.";

		return terNO_DST;
	}

	STAmount		saLimitAllow	= saLimitAmount;
		saLimitAllow.setIssuer(mTxnAccountID);

	SLE::pointer		sleRippleState	= entryCache(ltRIPPLE_STATE, Ledger::getRippleStateIndex(mTxnAccountID, uDstAccountID, uCurrencyID));
	if (sleRippleState)
	{
		// A line exists in one or more directions.
#if 0
		if (!saLimitAmount)
		{
			// Zeroing line.
			uint160		uLowID			= sleRippleState->getFieldAmount(sfLowLimit).getIssuer();
			uint160		uHighID			= sleRippleState->getFieldAmount(sfHighLimit).getIssuer();
			bool		bLow			= uLowID == uSrcAccountID;
			bool		bHigh			= uLowID == uDstAccountID;
			bool		bBalanceZero	= !sleRippleState->getFieldAmount(sfBalance);
			STAmount	saDstLimit		= sleRippleState->getFieldAmount(bSendLow ? sfLowLimit : sfHighLimit);
			bool		bDstLimitZero	= !saDstLimit;

			assert(bLow || bHigh);

			if (bBalanceZero && bDstLimitZero)
			{
				// Zero balance and eliminating last limit.

				bDelIndex	= true;
				terResult	= dirDelete(false, uSrcRef, Ledger::getOwnerDirIndex(mTxnAccountID), sleRippleState->getIndex(), false);
			}
		}
#endif

		if (!bDelIndex)
		{
			sleRippleState->setFieldAmount(bFlipped ? sfHighLimit: sfLowLimit, saLimitAllow);

			if (!bQualityIn)
			{
				nothing();
			}
			else if (uQualityIn)
			{
				sleRippleState->setFieldU32(bFlipped ? sfLowQualityIn : sfHighQualityIn, uQualityIn);
			}
			else
			{
				sleRippleState->makeFieldAbsent(bFlipped ? sfLowQualityIn : sfHighQualityIn);
			}

			if (!bQualityOut)
			{
				nothing();
			}
			else if (uQualityOut)
			{
				sleRippleState->setFieldU32(bFlipped ? sfLowQualityOut : sfHighQualityOut, uQualityOut);
			}
			else
			{
				sleRippleState->makeFieldAbsent(bFlipped ? sfLowQualityOut : sfHighQualityOut);
			}

			entryModify(sleRippleState);
		}

		Log(lsINFO) << "doCreditSet: Modifying ripple line: bDelIndex=" << bDelIndex;
	}
	// Line does not exist.
	else if (!saLimitAmount)
	{
		Log(lsINFO) << "doCreditSet: Redundant: Setting non-existant ripple line to 0.";

		return terNO_LINE_NO_ZERO;
	}
	else
	{
		// Create a new ripple line.
		sleRippleState	= entryCreate(ltRIPPLE_STATE, Ledger::getRippleStateIndex(mTxnAccountID, uDstAccountID, uCurrencyID));

		Log(lsINFO) << "doCreditSet: Creating ripple line: " << sleRippleState->getIndex().ToString();

		sleRippleState->setFieldAmount(sfBalance, STAmount(uCurrencyID, ACCOUNT_ONE));	// Zero balance in currency.
		sleRippleState->setFieldAmount(bFlipped ? sfHighLimit : sfLowLimit, saLimitAllow);
		sleRippleState->setFieldAmount(bFlipped ? sfLowLimit : sfHighLimit, STAmount(uCurrencyID, uDstAccountID));

		if (uQualityIn)
			sleRippleState->setFieldU32(bFlipped ? sfHighQualityIn : sfLowQualityIn, uQualityIn);
		if (uQualityOut)
			sleRippleState->setFieldU32(bFlipped ? sfHighQualityOut : sfLowQualityOut, uQualityOut);

		uint64			uSrcRef;							// Ignored, dirs never delete.

		terResult	= mNodes.dirAdd(uSrcRef, Ledger::getOwnerDirIndex(mTxnAccountID), sleRippleState->getIndex());

		if (tesSUCCESS == terResult)
			terResult	= mNodes.dirAdd(uSrcRef, Ledger::getOwnerDirIndex(uDstAccountID), sleRippleState->getIndex());
	}

	Log(lsINFO) << "doCreditSet<";

	return terResult;
}

TER TransactionEngine::doNicknameSet(const SerializedTransaction& txn)
{
	std::cerr << "doNicknameSet>" << std::endl;

	const uint256		uNickname		= txn.getFieldH256(sfNickname);
	const bool			bMinOffer		= txn.isFieldPresent(sfMinimumOffer);
	const STAmount		saMinOffer		= bMinOffer ? txn.getFieldAmount(sfAmount) : STAmount();

	SLE::pointer		sleNickname		= entryCache(ltNICKNAME, uNickname);

	if (sleNickname)
	{
		// Edit old entry.
		sleNickname->setFieldAccount(sfAccount, mTxnAccountID);

		if (bMinOffer && saMinOffer)
		{
			sleNickname->setFieldAmount(sfMinimumOffer, saMinOffer);
		}
		else
		{
			sleNickname->makeFieldAbsent(sfMinimumOffer);
		}

		entryModify(sleNickname);
	}
	else
	{
		// Make a new entry.
		// XXX Need to include authorization limiting for first year.

		sleNickname	= entryCreate(ltNICKNAME, Ledger::getNicknameIndex(uNickname));

		std::cerr << "doNicknameSet: Creating nickname node: " << sleNickname->getIndex().ToString() << std::endl;

		sleNickname->setFieldAccount(sfAccount, mTxnAccountID);

		if (bMinOffer && saMinOffer)
			sleNickname->setFieldAmount(sfMinimumOffer, saMinOffer);
	}

	std::cerr << "doNicknameSet<" << std::endl;

	return tesSUCCESS;
}

TER TransactionEngine::doPasswordFund(const SerializedTransaction& txn)
{
	std::cerr << "doPasswordFund>" << std::endl;

	const uint160		uDstAccountID	= txn.getFieldAccount160(sfDestination);
	SLE::pointer		sleDst			= mTxnAccountID == uDstAccountID
											? mTxnAccount
											: entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));
	if (!sleDst)
	{
		// Destination account does not exist.
		std::cerr << "doPasswordFund: Delay transaction: Destination account does not exist." << std::endl;

		return terSET_MISSING_DST;
	}

	if (sleDst->getFlags() & lsfPasswordSpent)
	{
		sleDst->clearFlag(lsfPasswordSpent);

		std::cerr << "doPasswordFund: Clearing spent." << sleDst->getFlags() << std::endl;

		if (mTxnAccountID != uDstAccountID) {
			std::cerr << "doPasswordFund: Destination modified." << std::endl;

			entryModify(sleDst);
		}
	}

	std::cerr << "doPasswordFund<" << std::endl;

	return tesSUCCESS;
}

TER TransactionEngine::doPasswordSet(const SerializedTransaction& txn)
{
	std::cerr << "doPasswordSet>" << std::endl;

	if (mTxnAccount->getFlags() & lsfPasswordSpent)
	{
		std::cerr << "doPasswordSet: Delay transaction: Funds already spent." << std::endl;

		return terFUNDS_SPENT;
	}

	mTxnAccount->setFlag(lsfPasswordSpent);

	TER	terResult	= setAuthorized(txn, false);

	std::cerr << "doPasswordSet<" << std::endl;

	return terResult;
}


// XXX Need to audit for things like setting accountID not having memory.
TER TransactionEngine::doPayment(const SerializedTransaction& txn, const TransactionEngineParams params)
{
	// Ripple if source or destination is non-native or if there are paths.
	const uint32	uTxFlags		= txn.getFlags();
	const bool		bCreate			= isSetBit(uTxFlags, tfCreateAccount);
	const bool		bPartialPayment	= isSetBit(uTxFlags, tfPartialPayment);
	const bool		bLimitQuality	= isSetBit(uTxFlags, tfLimitQuality);
	const bool		bNoRippleDirect	= isSetBit(uTxFlags, tfNoRippleDirect);
	const bool		bPaths			= txn.isFieldPresent(sfPaths);
	const bool		bMax			= txn.isFieldPresent(sfSendMax);
	const uint160	uDstAccountID	= txn.getFieldAccount160(sfDestination);
	const STAmount	saDstAmount		= txn.getFieldAmount(sfAmount);
	const STAmount	saMaxAmount		= bMax ? txn.getFieldAmount(sfSendMax) : saDstAmount;
	const uint160	uSrcCurrency	= saMaxAmount.getCurrency();
	const uint160	uDstCurrency	= saDstAmount.getCurrency();

	Log(lsINFO) << boost::str(boost::format("doPayment> saMaxAmount=%s saDstAmount=%s")
		% saMaxAmount.getFullText()
		% saDstAmount.getFullText());

	if (!uDstAccountID)
	{
		Log(lsINFO) << "doPayment: Invalid transaction: Payment destination account not specifed.";

		return temDST_NEEDED;
	}
	else if (!saDstAmount.isPositive())
	{
		Log(lsINFO) << "doPayment: Invalid transaction: bad amount: " << saDstAmount.getHumanCurrency() << " " << saDstAmount.getText();

		return temBAD_AMOUNT;
	}
	else if (mTxnAccountID == uDstAccountID && uSrcCurrency == uDstCurrency && !bPaths)
	{
		Log(lsINFO) << boost::str(boost::format("doPayment: Invalid transaction: Redunant transaction: src=%s, dst=%s, src_cur=%s, dst_cur=%s")
			% mTxnAccountID.ToString()
			% uDstAccountID.ToString()
			% uSrcCurrency.ToString()
			% uDstCurrency.ToString());

		return temREDUNDANT;
	}
	else if (bMax
		&& ((saMaxAmount == saDstAmount && saMaxAmount.getCurrency() == saDstAmount.getCurrency())
			|| (saDstAmount.isNative() && saMaxAmount.isNative())))
	{
		Log(lsINFO) << "doPayment: Invalid transaction: bad SendMax.";

		return temINVALID;
	}

	SLE::pointer	sleDst	= entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));
	if (!sleDst)
	{
		// Destination account does not exist.
		if (bCreate && !saDstAmount.isNative())
		{
			// This restriction could be relaxed.
			Log(lsINFO) << "doPayment: Invalid transaction: Create account may only fund XNS.";

			return temCREATEXNS;
		}
		else if (!bCreate)
		{
			Log(lsINFO) << "doPayment: Delay transaction: Destination account does not exist.";

			return terNO_DST;
		}

		// Create the account.
		sleDst	= entryCreate(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));

		sleDst->setFieldAccount(sfAccount, uDstAccountID);
		sleDst->setFieldU32(sfSequence, 1);
	}
	else
	{
		entryModify(sleDst);
	}

	TER			terResult;
	// XXX Should bMax be sufficient to imply ripple?
	const bool	bRipple	= bPaths || bMax || !saDstAmount.isNative();

	if (bRipple)
	{
		// Ripple payment

		STPathSet	spsPaths = txn.getFieldPathSet(sfPaths);
		STAmount	saMaxAmountAct;
		STAmount	saDstAmountAct;

		terResult	= isSetBit(params, tapOPEN_LEDGER) && spsPaths.getPathCount() > RIPPLE_PATHS_MAX
			? telBAD_PATH_COUNT
			: RippleCalc::rippleCalc(
				mNodes,
				saMaxAmountAct,
				saDstAmountAct,
				saMaxAmount,
				saDstAmount,
				uDstAccountID,
				mTxnAccountID,
				spsPaths,
				bPartialPayment,
				bLimitQuality,
				bNoRippleDirect);
	}
	else
	{
		// Direct XNS payment.

		STAmount	saSrcXNSBalance	= mTxnAccount->getFieldAmount(sfBalance);

		if (saSrcXNSBalance < saDstAmount)
		{
			// Transaction might succeed, if applied in a different order.
			Log(lsINFO) << "doPayment: Delay transaction: Insufficent funds.";

			terResult	= terUNFUNDED;
		}
		else
		{
			mTxnAccount->setFieldAmount(sfBalance, saSrcXNSBalance - saDstAmount);
			sleDst->setFieldAmount(sfBalance, sleDst->getFieldAmount(sfBalance) + saDstAmount);

			terResult	= tesSUCCESS;
		}
	}

	std::string	strToken;
	std::string	strHuman;

	if (transResultInfo(terResult, strToken, strHuman))
	{
		Log(lsINFO) << boost::str(boost::format("doPayment: %s: %s") % strToken % strHuman);
	}
	else
	{
		assert(false);
	}

	return terResult;
}

TER TransactionEngine::doWalletAdd(const SerializedTransaction& txn)
{
	std::cerr << "WalletAdd>" << std::endl;

	const std::vector<unsigned char>	vucPubKey		= txn.getFieldVL(sfPublicKey);
	const std::vector<unsigned char>	vucSignature	= txn.getFieldVL(sfSignature);
	const uint160						uAuthKeyID		= txn.getFieldAccount160(sfAuthorizedKey);
	const NewcoinAddress				naMasterPubKey	= NewcoinAddress::createAccountPublic(vucPubKey);
	const uint160						uDstAccountID	= naMasterPubKey.getAccountID();

	if (!naMasterPubKey.accountPublicVerify(Serializer::getSHA512Half(uAuthKeyID.begin(), uAuthKeyID.size()), vucSignature))
	{
		std::cerr << "WalletAdd: unauthorized: bad signature " << std::endl;

		return tefBAD_ADD_AUTH;
	}

	SLE::pointer		sleDst	= entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));

	if (sleDst)
	{
		std::cerr << "WalletAdd: account already created" << std::endl;

		return tefCREATED;
	}

	STAmount			saAmount		= txn.getFieldAmount(sfAmount);
	STAmount			saSrcBalance	= mTxnAccount->getFieldAmount(sfBalance);

	if (saSrcBalance < saAmount)
	{
		std::cerr
			<< boost::str(boost::format("WalletAdd: Delay transaction: insufficent balance: balance=%s amount=%s")
				% saSrcBalance.getText()
				% saAmount.getText())
			<< std::endl;

		return terUNFUNDED;
	}

	// Deduct initial balance from source account.
	mTxnAccount->setFieldAmount(sfBalance, saSrcBalance-saAmount);

	// Create the account.
	sleDst	= entryCreate(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));

	sleDst->setFieldAccount(sfAccount, uDstAccountID);
	sleDst->setFieldU32(sfSequence, 1);
	sleDst->setFieldAmount(sfBalance, saAmount);
	sleDst->setFieldAccount(sfAuthorizedKey, uAuthKeyID);

	std::cerr << "WalletAdd<" << std::endl;

	return tesSUCCESS;
}

TER TransactionEngine::doInvoice(const SerializedTransaction& txn)
{
	return temUNKNOWN;
}

// Take as much as possible. Adjusts account balances. Charges fees on top to taker.
// -->   uBookBase: The order book to take against.
// --> saTakerPays: What the taker offers (w/ issuer)
// --> saTakerGets: What the taker wanted (w/ issuer)
// <-- saTakerPaid: What taker paid not including fees. To reduce an offer.
// <--  saTakerGot: What taker got not including fees. To reduce an offer.
// <--   terResult: tesSUCCESS or terNO_ACCOUNT
// XXX: Fees should be paid by the source of the currency.
TER TransactionEngine::takeOffers(
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

	Log(lsINFO) << "takeOffers: against book: " << uBookBase.ToString();

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

			sleOfferDir		= entryCache(ltDIR_NODE, mLedger->getNextLedgerIndex(uTipIndex, uBookEnd));
			if (sleOfferDir)
			{
				Log(lsINFO) << "takeOffers: possible counter offer found";

				uTipIndex		= sleOfferDir->getIndex();
				uTipQuality		= Ledger::getQuality(uTipIndex);
			}
			else
			{
				Log(lsINFO) << "takeOffers: counter offer book is empty: "
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
			Log(lsINFO) << "takeOffers: done";

			terResult	= tesSUCCESS;
		}
		else
		{
			// Have an offer directory to consider.
			Log(lsINFO) << "takeOffers: considering dir: " << sleOfferDir->getJson(0);

			SLE::pointer	sleBookNode;
			unsigned int	uBookEntry;
			uint256			uOfferIndex;

			mNodes.dirFirst(uTipIndex, sleBookNode, uBookEntry, uOfferIndex);

			SLE::pointer	sleOffer		= entryCache(ltOFFER, uOfferIndex);

			Log(lsINFO) << "takeOffers: considering offer : " << sleOffer->getJson(0);

			const uint160	uOfferOwnerID	= sleOffer->getFieldAccount(sfAccount).getAccountID();
			STAmount		saOfferPays		= sleOffer->getFieldAmount(sfTakerGets);
			STAmount		saOfferGets		= sleOffer->getFieldAmount(sfTakerPays);

			if (sleOffer->isFieldPresent(sfExpiration) && sleOffer->getFieldU32(sfExpiration) <= mLedger->getParentCloseTimeNC())
			{
				// Offer is expired. Expired offers are considered unfunded. Delete it.
				Log(lsINFO) << "takeOffers: encountered expired offer";

				usOfferUnfundedFound.insert(uOfferIndex);
			}
			else if (uOfferOwnerID == uTakerAccountID)
			{
				// Would take own offer. Consider old offer expired. Delete it.
				Log(lsINFO) << "takeOffers: encountered taker's own old offer";

				usOfferUnfundedFound.insert(uOfferIndex);
			}
			else
			{
				// Get offer funds available.

				Log(lsINFO) << "takeOffers: saOfferPays=" << saOfferPays.getFullText();

				STAmount		saOfferFunds	= mNodes.accountFunds(uOfferOwnerID, saOfferPays);
				STAmount		saTakerFunds	= mNodes.accountFunds(uTakerAccountID, saTakerPays);
				SLE::pointer	sleOfferAccount;	// Owner of offer.

				if (!saOfferFunds.isPositive())
				{
					// Offer is unfunded, possibly due to previous balance action.
					Log(lsINFO) << "takeOffers: offer unfunded: delete";

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

					Log(lsINFO) << "takeOffers: applyOffer:    saTakerPays: " << saTakerPays.getFullText();
					Log(lsINFO) << "takeOffers: applyOffer:    saTakerPaid: " << saTakerPaid.getFullText();
					Log(lsINFO) << "takeOffers: applyOffer:   saTakerFunds: " << saTakerFunds.getFullText();
					Log(lsINFO) << "takeOffers: applyOffer:   saOfferFunds: " << saOfferFunds.getFullText();
					Log(lsINFO) << "takeOffers: applyOffer:          saPay: " << saPay.getFullText();
					Log(lsINFO) << "takeOffers: applyOffer:    saOfferPays: " << saOfferPays.getFullText();
					Log(lsINFO) << "takeOffers: applyOffer:    saOfferGets: " << saOfferGets.getFullText();
					Log(lsINFO) << "takeOffers: applyOffer:    saTakerPays: " << saTakerPays.getFullText();
					Log(lsINFO) << "takeOffers: applyOffer:    saTakerGets: " << saTakerGets.getFullText();

					bool	bOfferDelete	= STAmount::applyOffer(
						mNodes.rippleTransferRate(uTakerAccountID, uOfferOwnerID, uTakerPaysAccountID),
						mNodes.rippleTransferRate(uOfferOwnerID, uTakerAccountID, uTakerGetsAccountID),
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

					Log(lsINFO) << "takeOffers: applyOffer: saSubTakerPaid: " << saSubTakerPaid.getFullText();
					Log(lsINFO) << "takeOffers: applyOffer:  saSubTakerGot: " << saSubTakerGot.getFullText();

					// Adjust offer

					// Offer owner will pay less.  Subtract what taker just got.
					sleOffer->setFieldAmount(sfTakerGets, saOfferPays -= saSubTakerGot);

					// Offer owner will get less.  Subtract what owner just paid.
					sleOffer->setFieldAmount(sfTakerPays, saOfferGets -= saSubTakerPaid);

					entryModify(sleOffer);

					if (bOfferDelete)
					{
						// Offer now fully claimed or now unfunded.
						Log(lsINFO) << "takeOffers: offer claimed: delete";

						usOfferUnfundedBecame.insert(uOfferIndex);	// Delete unfunded offer on success.

						// Offer owner's account is no longer pristine.
						usAccountTouched.insert(uOfferOwnerID);
					}
					else
					{
						Log(lsINFO) << "takeOffers: offer partial claim.";
					}

					// Offer owner pays taker.
					// saSubTakerGot.setIssuer(uTakerGetsAccountID);	// XXX Move this earlier?
					assert(!!saSubTakerGot.getIssuer());

					mNodes.accountSend(uOfferOwnerID, uTakerAccountID, saSubTakerGot);
					mNodes.accountSend(uOfferOwnerID, uTakerGetsAccountID, saOfferIssuerFee);

					saTakerGot	+= saSubTakerGot;

					// Taker pays offer owner.
					//	saSubTakerPaid.setIssuer(uTakerPaysAccountID);
					assert(!!saSubTakerPaid.getIssuer());

					mNodes.accountSend(uTakerAccountID, uOfferOwnerID, saSubTakerPaid);
					mNodes.accountSend(uTakerAccountID, uTakerPaysAccountID, saTakerIssuerFee);

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
			terResult	= mNodes.offerDelete(uOfferIndex);
			if (tesSUCCESS != terResult)
				break;
		}
	}

	if (tesSUCCESS == terResult)
	{
		// On success, delete offers that became unfunded.
		BOOST_FOREACH(const uint256& uOfferIndex, usOfferUnfundedBecame)
		{
			terResult	= mNodes.offerDelete(uOfferIndex);
			if (tesSUCCESS != terResult)
				break;
		}
	}

	return terResult;
}

TER TransactionEngine::doOfferCreate(const SerializedTransaction& txn)
{
Log(lsWARNING) << "doOfferCreate> " << txn.getJson(0);
	const uint32			txFlags			= txn.getFlags();
	const bool				bPassive		= isSetBit(txFlags, tfPassive);
	STAmount				saTakerPays		= txn.getFieldAmount(sfTakerPays);
	STAmount				saTakerGets		= txn.getFieldAmount(sfTakerGets);

Log(lsINFO) << boost::str(boost::format("doOfferCreate: saTakerPays=%s saTakerGets=%s")
	% saTakerPays.getFullText()
	% saTakerGets.getFullText());

	const uint160			uPaysIssuerID	= saTakerPays.getIssuer();
	const uint160			uGetsIssuerID	= saTakerGets.getIssuer();
	const uint32			uExpiration		= txn.getFieldU32(sfExpiration);
	const bool				bHaveExpiration	= txn.isFieldPresent(sfExpiration);
	const uint32			uSequence		= txn.getSequence();

	const uint256			uLedgerIndex	= Ledger::getOfferIndex(mTxnAccountID, uSequence);
	SLE::pointer			sleOffer		= entryCreate(ltOFFER, uLedgerIndex);

	Log(lsINFO) << "doOfferCreate: Creating offer node: " << uLedgerIndex.ToString() << " uSequence=" << uSequence;

	const uint160			uPaysCurrency	= saTakerPays.getCurrency();
	const uint160			uGetsCurrency	= saTakerGets.getCurrency();
	const uint64			uRate			= STAmount::getRate(saTakerGets, saTakerPays);

	TER						terResult		= tesSUCCESS;
	uint256					uDirectory;		// Delete hints.
	uint64					uOwnerNode;
	uint64					uBookNode;

	if (bHaveExpiration && !uExpiration)
	{
		Log(lsWARNING) << "doOfferCreate: Malformed offer: bad expiration";

		terResult	= temBAD_EXPIRATION;
	}
	else if (bHaveExpiration && mLedger->getParentCloseTimeNC() >= uExpiration)
	{
		Log(lsWARNING) << "doOfferCreate: Expired transaction: offer expired";

		// XXX CHARGE FEE ONLY.
		terResult	= tesSUCCESS;
	}
	else if (saTakerPays.isNative() && saTakerGets.isNative())
	{
		Log(lsWARNING) << "doOfferCreate: Malformed offer: XNS for XNS";

		terResult	= temBAD_OFFER;
	}
	else if (!saTakerPays.isPositive() || !saTakerGets.isPositive())
	{
		Log(lsWARNING) << "doOfferCreate: Malformed offer: bad amount";

		terResult	= temBAD_OFFER;
	}
	else if (uPaysCurrency == uGetsCurrency && uPaysIssuerID == uGetsIssuerID)
	{
		Log(lsWARNING) << "doOfferCreate: Malformed offer: redundant offer";

		terResult	= temREDUNDANT;
	}
	else if (saTakerPays.isNative() != !uPaysIssuerID || saTakerGets.isNative() != !uGetsIssuerID)
	{
		Log(lsWARNING) << "doOfferCreate: Malformed offer: bad issuer";

		terResult	= temBAD_ISSUER;
	}
	else if (!mNodes.accountFunds(mTxnAccountID, saTakerGets).isPositive())
	{
		Log(lsWARNING) << "doOfferCreate: delay: Offers must be at least partially funded.";

		terResult	= terUNFUNDED;
	}

	if (tesSUCCESS == terResult && !saTakerPays.isNative())
	{
		SLE::pointer		sleTakerPays	= entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uPaysIssuerID));

		if (!sleTakerPays)
		{
			Log(lsWARNING) << "doOfferCreate: delay: can't receive IOUs from non-existant issuer: " << NewcoinAddress::createHumanAccountID(uPaysIssuerID);

			terResult	= terNO_ACCOUNT;
		}
	}

	if (tesSUCCESS == terResult)
	{
		STAmount		saOfferPaid;
		STAmount		saOfferGot;
		const uint256	uTakeBookBase	= Ledger::getBookBase(uGetsCurrency, uGetsIssuerID, uPaysCurrency, uPaysIssuerID);

		Log(lsINFO) << boost::str(boost::format("doOfferCreate: take against book: %s for %s -> %s")
			% uTakeBookBase.ToString()
			% saTakerGets.getFullText()
			% saTakerPays.getFullText());

		// Take using the parameters of the offer.
#if 1
		Log(lsWARNING) << "doOfferCreate: takeOffers: BEFORE saTakerGets=" << saTakerGets.getFullText();
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
#else
		terResult	= tesSUCCESS;
#endif
		Log(lsWARNING) << "doOfferCreate: takeOffers=" << terResult;
		Log(lsWARNING) << "doOfferCreate: takeOffers: saOfferPaid=" << saOfferPaid.getFullText();
		Log(lsWARNING) << "doOfferCreate: takeOffers:  saOfferGot=" << saOfferGot.getFullText();
		Log(lsWARNING) << "doOfferCreate: takeOffers: saTakerPays=" << saTakerPays.getFullText();
		Log(lsWARNING) << "doOfferCreate: takeOffers: AFTER saTakerGets=" << saTakerGets.getFullText();

		if (tesSUCCESS == terResult)
		{
			saTakerPays		-= saOfferGot;				// Reduce payin from takers by what offer just got.
			saTakerGets		-= saOfferPaid;				// Reduce payout to takers by what srcAccount just paid.
		}
	}

	Log(lsWARNING) << "doOfferCreate: takeOffers: saTakerPays=" << saTakerPays.getFullText();
	Log(lsWARNING) << "doOfferCreate: takeOffers: saTakerGets=" << saTakerGets.getFullText();
	Log(lsWARNING) << "doOfferCreate: takeOffers: mTxnAccountID=" << NewcoinAddress::createHumanAccountID(mTxnAccountID);
	Log(lsWARNING) << "doOfferCreate: takeOffers:         FUNDS=" << mNodes.accountFunds(mTxnAccountID, saTakerGets).getFullText();

	// Log(lsWARNING) << "doOfferCreate: takeOffers: uPaysIssuerID=" << NewcoinAddress::createHumanAccountID(uPaysIssuerID);
	// Log(lsWARNING) << "doOfferCreate: takeOffers: uGetsIssuerID=" << NewcoinAddress::createHumanAccountID(uGetsIssuerID);

	if (tesSUCCESS == terResult
		&& saTakerPays														// Still wanting something.
		&& saTakerGets														// Still offering something.
		&& mNodes.accountFunds(mTxnAccountID, saTakerGets).isPositive())	// Still funded.
	{
		// We need to place the remainder of the offer into its order book.
		Log(lsINFO) << boost::str(boost::format("doOfferCreate: offer not fully consumed: saTakerPays=%s saTakerGets=%s")
			% saTakerPays.getFullText()
			% saTakerGets.getFullText());

		// Add offer to owner's directory.
		terResult	= mNodes.dirAdd(uOwnerNode, Ledger::getOwnerDirIndex(mTxnAccountID), uLedgerIndex);

		if (tesSUCCESS == terResult)
		{
			uint256	uBookBase	= Ledger::getBookBase(uPaysCurrency, uPaysIssuerID, uGetsCurrency, uGetsIssuerID);

			Log(lsINFO) << boost::str(boost::format("doOfferCreate: adding to book: %s : %s/%s -> %s/%s")
				% uBookBase.ToString()
				% saTakerPays.getHumanCurrency()
				% NewcoinAddress::createHumanAccountID(saTakerPays.getIssuer())
				% saTakerGets.getHumanCurrency()
				% NewcoinAddress::createHumanAccountID(saTakerGets.getIssuer()));

			uDirectory	= Ledger::getQualityIndex(uBookBase, uRate);	// Use original rate.

			// Add offer to order book.
			terResult	= mNodes.dirAdd(uBookNode, uDirectory, uLedgerIndex);
		}

		if (tesSUCCESS == terResult)
		{
			Log(lsWARNING) << "doOfferCreate: sfAccount=" << NewcoinAddress::createHumanAccountID(mTxnAccountID);
			Log(lsWARNING) << "doOfferCreate: uPaysIssuerID=" << NewcoinAddress::createHumanAccountID(uPaysIssuerID);
			Log(lsWARNING) << "doOfferCreate: uGetsIssuerID=" << NewcoinAddress::createHumanAccountID(uGetsIssuerID);
			Log(lsWARNING) << "doOfferCreate: saTakerPays.isNative()=" << saTakerPays.isNative();
			Log(lsWARNING) << "doOfferCreate: saTakerGets.isNative()=" << saTakerGets.isNative();
			Log(lsWARNING) << "doOfferCreate: uPaysCurrency=" << saTakerPays.getHumanCurrency();
			Log(lsWARNING) << "doOfferCreate: uGetsCurrency=" << saTakerGets.getHumanCurrency();

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
		}
	}

	Log(lsINFO) << "doOfferCreate: final sleOffer=" << sleOffer->getJson(0);

	return terResult;
}

TER TransactionEngine::doOfferCancel(const SerializedTransaction& txn)
{
	TER				terResult;
	const uint32	uSequence	= txn.getFieldU32(sfOfferSequence);
	const uint256	uOfferIndex	= Ledger::getOfferIndex(mTxnAccountID, uSequence);
	SLE::pointer	sleOffer	= entryCache(ltOFFER, uOfferIndex);

	if (sleOffer)
	{
		Log(lsWARNING) << "doOfferCancel: uSequence=" << uSequence;

		terResult	= mNodes.offerDelete(sleOffer, uOfferIndex, mTxnAccountID);
	}
	else
	{
		Log(lsWARNING) << "doOfferCancel: offer not found: "
			<< NewcoinAddress::createHumanAccountID(mTxnAccountID)
			<< " : " << uSequence
			<< " : " << uOfferIndex.ToString();

		terResult	= terOFFER_NOT_FOUND;
	}

	return terResult;
}

TER	TransactionEngine::doContractAdd(const SerializedTransaction& txn)
{
	Log(lsWARNING) << "doContractAdd> " << txn.getJson(0);

	const uint32 expiration		= txn.getFieldU32(sfExpiration);
//	const uint32 bondAmount		= txn.getFieldU32(sfBondAmount);
//	const uint32 stampEscrow	= txn.getFieldU32(sfStampEscrow);
	STAmount rippleEscrow		= txn.getFieldAmount(sfRippleEscrow);
	std::vector<unsigned char>	createCode		= txn.getFieldVL(sfCreateCode);
	std::vector<unsigned char>	fundCode		= txn.getFieldVL(sfFundCode);
	std::vector<unsigned char>	removeCode		= txn.getFieldVL(sfRemoveCode);
	std::vector<unsigned char>	expireCode		= txn.getFieldVL(sfExpireCode);

	// make sure
	// expiration hasn't passed
	// bond amount is enough
	// they have the stamps for the bond

	// place contract in ledger
	// run create code
	

	if (mLedger->getParentCloseTimeNC() >= expiration)
	{
		Log(lsWARNING) << "doContractAdd: Expired transaction: offer expired";
		return(tefALREADY);
	}
	//TODO: check bond
	//if( txn.getSourceAccount() )

	Contract contract;
	Script::Interpreter interpreter;
	TER	terResult=interpreter.interpret(&contract,txn,createCode);
	if(tesSUCCESS != terResult)
	{

	}

	return(terResult);
}

TER	TransactionEngine::doContractRemove(const SerializedTransaction& txn)
{
	// TODO:
	return(tesSUCCESS);
}

// vim:ts=4
