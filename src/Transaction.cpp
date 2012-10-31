#include <cassert>

#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/make_shared.hpp>
#include <boost/ref.hpp>

#include "Application.h"
#include "Transaction.h"
#include "Wallet.h"
#include "BitcoinUtil.h"
#include "Serializer.h"
#include "SerializedTransaction.h"
#include "Log.h"

DECLARE_INSTANCE(Transaction);

Transaction::Transaction(SerializedTransaction::ref sit, bool bValidate)
	: mInLedger(0), mStatus(INVALID), mResult(temUNCERTAIN), mTransaction(sit)
{
	try
	{
		mFromPubKey.setAccountPublic(mTransaction->getSigningPubKey());
		mTransactionID	= mTransaction->getTransactionID();
		mAccountFrom	= mTransaction->getSourceAccount();
	}
	catch(...)
	{
		return;
	}

	if (!bValidate || checkSign())
		mStatus = NEW;
}

Transaction::pointer Transaction::sharedTransaction(const std::vector<unsigned char>&vucTransaction, bool bValidate)
{
	try
	{
		Serializer			s(vucTransaction);
		SerializerIterator	sit(s);

		SerializedTransaction::pointer	st	= boost::make_shared<SerializedTransaction>(boost::ref(sit));

		return boost::make_shared<Transaction>(st, bValidate);
	}
	catch (...)
	{
		Log(lsWARNING) << "Exception constructing transaction";
		return boost::shared_ptr<Transaction>();
	}
}

//
// Generic transaction construction
//

Transaction::Transaction(
	TransactionType ttKind,
	const RippleAddress&	naPublicKey,
	const RippleAddress&	naSourceAccount,
	uint32					uSeq,
	const STAmount&			saFee,
	uint32					uSourceTag) :
		mAccountFrom(naSourceAccount), mFromPubKey(naPublicKey), mStatus(NEW), mResult(temUNCERTAIN)
{
	assert(mFromPubKey.isValid());

	mTransaction	= boost::make_shared<SerializedTransaction>(ttKind);

	// Log(lsINFO) << str(boost::format("Transaction: account: %s") % naSourceAccount.humanAccountID());
	// Log(lsINFO) << str(boost::format("Transaction: mAccountFrom: %s") % mAccountFrom.humanAccountID());

	mTransaction->setSigningPubKey(mFromPubKey);
	mTransaction->setSourceAccount(mAccountFrom);
	mTransaction->setSequence(uSeq);
	mTransaction->setTransactionFee(saFee);

	if (uSourceTag)
	{
		mTransaction->makeFieldPresent(sfSourceTag);
		mTransaction->setFieldU32(sfSourceTag, uSourceTag);
	}
}

bool Transaction::sign(const RippleAddress& naAccountPrivate)
{
	bool	bResult	= true;

	if (!naAccountPrivate.isValid())
	{
		Log(lsWARNING) << "No private key for signing";
		bResult	= false;
	}
	getSTransaction()->sign(naAccountPrivate);

	if (bResult)
	{
		updateID();
	}
	else
	{
		mStatus = INCOMPLETE;
	}

	return bResult;
}

//
// AccountSet
//

Transaction::pointer Transaction::setAccountSet(
	const RippleAddress& naPrivateKey,
	bool								bEmailHash,
	const uint128&						uEmailHash,
	bool								bWalletLocator,
	const uint256&						uWalletLocator,
	const RippleAddress&				naMessagePublic,
	bool								bDomain,
	const std::vector<unsigned char>&	vucDomain,
	bool								bTransferRate,
	const uint32						uTransferRate,
	bool								bPublish,
	const uint256&						uPublishHash,
	const uint32						uPublishSize
	)
{
	if (!bEmailHash)
		mTransaction->setFieldH128(sfEmailHash, uEmailHash);

	if (!bWalletLocator)
		mTransaction->setFieldH256(sfWalletLocator, uWalletLocator);

	if (naMessagePublic.isValid())
		mTransaction->setFieldVL(sfMessageKey, naMessagePublic.getAccountPublic());

	if (bDomain)
		mTransaction->setFieldVL(sfDomain, vucDomain);

	if (bTransferRate)
		mTransaction->setFieldU32(sfTransferRate, uTransferRate);

	if (bPublish)
	{
		mTransaction->setFieldH256(sfPublishHash, uPublishHash);
		mTransaction->setFieldU32(sfPublishSize, uPublishSize);
	}

	sign(naPrivateKey);

	return shared_from_this();
}

Transaction::pointer Transaction::sharedAccountSet(
	const RippleAddress& naPublicKey, const RippleAddress& naPrivateKey,
	const RippleAddress& naSourceAccount,
	uint32								uSeq,
	const STAmount&						saFee,
	uint32								uSourceTag,
	bool								bEmailHash,
	const uint128&						uEmailHash,
	bool								bWalletLocator,
	const uint256&						uWalletLocator,
	const RippleAddress&				naMessagePublic,
	bool								bDomain,
	const std::vector<unsigned char>&	vucDomain,
	bool								bTransferRate,
	const uint32						uTransferRate,
	bool								bPublish,
	const uint256&						uPublishHash,
	const uint32						uPublishSize)
{
	pointer	tResult	= boost::make_shared<Transaction>(ttACCOUNT_SET, naPublicKey, naSourceAccount, uSeq, saFee, uSourceTag);

	return tResult->setAccountSet(naPrivateKey, bEmailHash, uEmailHash, bWalletLocator, uWalletLocator,
		naMessagePublic,
		bDomain, vucDomain, bTransferRate, uTransferRate, bPublish, uPublishHash, uPublishSize);
}

//
// Claim
//

Transaction::pointer Transaction::setClaim(
	const RippleAddress& naPrivateKey,
	const std::vector<unsigned char>& vucGenerator,
	const std::vector<unsigned char>& vucPubKey,
	const std::vector<unsigned char>& vucSignature)
{
	mTransaction->setFieldVL(sfGenerator, vucGenerator);
	mTransaction->setFieldVL(sfPublicKey, vucPubKey);
	mTransaction->setFieldVL(sfSignature, vucSignature);

	sign(naPrivateKey);

	return shared_from_this();
}

Transaction::pointer Transaction::sharedClaim(
	const RippleAddress& naPublicKey, const RippleAddress& naPrivateKey,
	uint32 uSourceTag,
	const std::vector<unsigned char>& vucGenerator,
	const std::vector<unsigned char>& vucPubKey,
	const std::vector<unsigned char>& vucSignature)
{
	pointer	tResult	= boost::make_shared<Transaction>(ttCLAIM,
						naPublicKey, naPublicKey,
						0,		// Sequence of 0.
						0,		// Free.
						uSourceTag);

	return tResult->setClaim(naPrivateKey, vucGenerator, vucPubKey, vucSignature);
}

//
// Create
//

Transaction::pointer Transaction::setCreate(
	const RippleAddress&	naPrivateKey,
	const RippleAddress&	naCreateAccountID,
	const STAmount&			saFund)
{
	mTransaction->setFieldU32(sfFlags, tfCreateAccount);
	mTransaction->setFieldAccount(sfDestination, naCreateAccountID);
	mTransaction->setFieldAmount(sfAmount, saFund);

	sign(naPrivateKey);

	return shared_from_this();
}

Transaction::pointer Transaction::sharedCreate(
	const RippleAddress& naPublicKey, const RippleAddress& naPrivateKey,
	const RippleAddress& naSourceAccount,
	uint32					uSeq,
	const STAmount&			saFee,
	uint32					uSourceTag,
	const RippleAddress&	naCreateAccountID,
	const STAmount&			saFund)
{
	pointer	tResult	= boost::make_shared<Transaction>(ttPAYMENT, naPublicKey, naSourceAccount, uSeq, saFee, uSourceTag);

	return tResult->setCreate(naPrivateKey, naCreateAccountID, saFund);
}

//
// CreditSet
//

Transaction::pointer Transaction::setCreditSet(
	const RippleAddress&	naPrivateKey,
	const STAmount&			saLimitAmount,
	bool					bQualityIn,
	uint32					uQualityIn,
	bool					bQualityOut,
	uint32					uQualityOut)
{
	mTransaction->setFieldAmount(sfLimitAmount, saLimitAmount);

	if (bQualityIn)
		mTransaction->setFieldU32(sfQualityIn, uQualityIn);

	if (bQualityOut)
		mTransaction->setFieldU32(sfQualityOut, uQualityOut);

	sign(naPrivateKey);

	return shared_from_this();
}

Transaction::pointer Transaction::sharedCreditSet(
	const RippleAddress& naPublicKey, const RippleAddress& naPrivateKey,
	const RippleAddress&	naSourceAccount,
	uint32					uSeq,
	const STAmount&			saFee,
	uint32					uSourceTag,
	const STAmount&			saLimitAmount,
	bool					bQualityIn,
	uint32					uQualityIn,
	bool					bQualityOut,
	uint32					uQualityOut)
{
	pointer	tResult	= boost::make_shared<Transaction>(ttCREDIT_SET, naPublicKey, naSourceAccount, uSeq, saFee, uSourceTag);

	return tResult->setCreditSet(naPrivateKey,
		saLimitAmount,
		bQualityIn, uQualityIn,
		bQualityOut, uQualityOut);
}

//
// NicknameSet
//

Transaction::pointer Transaction::setNicknameSet(
	const RippleAddress&				naPrivateKey,
	const uint256&						uNickname,
	bool								bSetOffer,
	const STAmount&						saMinimumOffer)
{
	mTransaction->setFieldH256(sfNickname, uNickname);

	// XXX Make sure field is present even for 0!
	if (bSetOffer)
		mTransaction->setFieldAmount(sfMinimumOffer, saMinimumOffer);

	sign(naPrivateKey);

	return shared_from_this();
}

// --> bSetOffer: true, change offer
// --> saMinimumOffer: 0 to remove.
Transaction::pointer Transaction::sharedNicknameSet(
	const RippleAddress& naPublicKey, const RippleAddress& naPrivateKey,
	const RippleAddress&				naSourceAccount,
	uint32								uSeq,
	const STAmount&						saFee,
	uint32								uSourceTag,
	const uint256&						uNickname,
	bool								bSetOffer,
	const STAmount&						saMinimumOffer)
{
	pointer	tResult	= boost::make_shared<Transaction>(ttNICKNAME_SET, naPublicKey, naSourceAccount, uSeq, saFee, uSourceTag);

	return tResult->setNicknameSet(naPrivateKey, uNickname, bSetOffer, saMinimumOffer);
}

//
// OfferCreate
//

Transaction::pointer Transaction::setOfferCreate(
	const RippleAddress&				naPrivateKey,
	bool								bPassive,
	const STAmount&						saTakerPays,
	const STAmount&						saTakerGets,
	uint32								uExpiration)
{
	if (bPassive)
		mTransaction->setFieldU32(sfFlags, tfPassive);

	mTransaction->setFieldAmount(sfTakerPays, saTakerPays);
	mTransaction->setFieldAmount(sfTakerGets, saTakerGets);

	if (uExpiration)
		mTransaction->setFieldU32(sfExpiration, uExpiration);

	sign(naPrivateKey);

	return shared_from_this();
}

Transaction::pointer Transaction::sharedOfferCreate(
	const RippleAddress& naPublicKey, const RippleAddress& naPrivateKey,
	const RippleAddress&				naSourceAccount,
	uint32								uSeq,
	const STAmount&						saFee,
	uint32								uSourceTag,
	bool								bPassive,
	const STAmount&						saTakerPays,
	const STAmount&						saTakerGets,
	uint32								uExpiration)
{
	pointer	tResult	= boost::make_shared<Transaction>(ttOFFER_CREATE, naPublicKey, naSourceAccount, uSeq, saFee, uSourceTag);

	return tResult->setOfferCreate(naPrivateKey, bPassive, saTakerPays, saTakerGets, uExpiration);
}

//
// OfferCancel
//

Transaction::pointer Transaction::setOfferCancel(
	const RippleAddress&				naPrivateKey,
	uint32								uSequence)
{
	mTransaction->setFieldU32(sfOfferSequence, uSequence);

	sign(naPrivateKey);

	return shared_from_this();
}

Transaction::pointer Transaction::sharedOfferCancel(
	const RippleAddress& naPublicKey, const RippleAddress& naPrivateKey,
	const RippleAddress&				naSourceAccount,
	uint32								uSeq,
	const STAmount&						saFee,
	uint32								uSourceTag,
	uint32								uSequence)
{
	pointer	tResult	= boost::make_shared<Transaction>(ttOFFER_CANCEL, naPublicKey, naSourceAccount, uSeq, saFee, uSourceTag);

	return tResult->setOfferCancel(naPrivateKey, uSequence);
}

//
// PasswordFund
//

Transaction::pointer Transaction::setPasswordFund(
	const RippleAddress&	naPrivateKey,
	const RippleAddress&	naDstAccountID)
{
	mTransaction->setFieldAccount(sfDestination, naDstAccountID);

	sign(naPrivateKey);

	return shared_from_this();
}

Transaction::pointer Transaction::sharedPasswordFund(
	const RippleAddress& naPublicKey, const RippleAddress& naPrivateKey,
	const RippleAddress&	naSourceAccount,
	uint32					uSeq,
	const STAmount&			saFee,
	uint32					uSourceTag,
	const RippleAddress&	naDstAccountID)
{
	pointer	tResult	= boost::make_shared<Transaction>(ttPASSWORD_FUND, naPublicKey, naSourceAccount, uSeq, saFee, uSourceTag);

	return tResult->setPasswordFund(naPrivateKey, naDstAccountID);
}

//
// PasswordSet
//

Transaction::pointer Transaction::setPasswordSet(
	const RippleAddress& naPrivateKey,
	const RippleAddress&				naAuthKeyID,
	const std::vector<unsigned char>&	vucGenerator,
	const std::vector<unsigned char>&	vucPubKey,
	const std::vector<unsigned char>&	vucSignature)
{
	mTransaction->setFieldAccount(sfAuthorizedKey, naAuthKeyID);
	mTransaction->setFieldVL(sfGenerator, vucGenerator);
	mTransaction->setFieldVL(sfPublicKey, vucPubKey);
	mTransaction->setFieldVL(sfSignature, vucSignature);

	sign(naPrivateKey);

	return shared_from_this();
}

Transaction::pointer Transaction::sharedPasswordSet(
	const RippleAddress& naPublicKey, const RippleAddress& naPrivateKey,
	uint32								uSourceTag,
	const RippleAddress&				naAuthKeyID,
	const std::vector<unsigned char>&	vucGenerator,
	const std::vector<unsigned char>&	vucPubKey,
	const std::vector<unsigned char>&	vucSignature)
{
	pointer	tResult	= boost::make_shared<Transaction>(ttPASSWORD_SET,
						naPublicKey, naPublicKey,
						0,		// Sequence of 0.
						0,		// Free.
						uSourceTag);

	return tResult->setPasswordSet(naPrivateKey, naAuthKeyID, vucGenerator, vucPubKey, vucSignature);
}

//
// Payment
//

Transaction::pointer Transaction::setPayment(
	const RippleAddress&	naPrivateKey,
	const RippleAddress&	naDstAccountID,
	const STAmount&			saAmount,
	const STAmount&			saSendMax,
	const STPathSet&		spsPaths,
	const bool				bPartial,
	const bool				bLimit)
{
	mTransaction->setFieldAccount(sfDestination, naDstAccountID);
	mTransaction->setFieldAmount(sfAmount, saAmount);

	if (saAmount != saSendMax || saAmount.getCurrency() != saSendMax.getCurrency())
	{
		mTransaction->setFieldAmount(sfSendMax, saSendMax);
	}

	if (spsPaths.getPathCount())
	{
		mTransaction->setFieldPathSet(sfPaths, spsPaths);
	}

	sign(naPrivateKey);

	return shared_from_this();
}

Transaction::pointer Transaction::sharedPayment(
	const RippleAddress& naPublicKey, const RippleAddress& naPrivateKey,
	const RippleAddress&	naSourceAccount,
	uint32					uSeq,
	const STAmount&			saFee,
	uint32					uSourceTag,
	const RippleAddress&	naDstAccountID,
	const STAmount&			saAmount,
	const STAmount&			saSendMax,
	const STPathSet&		spsPaths,
	const bool				bPartial,
	const bool				bLimit)
{
	pointer	tResult	= boost::make_shared<Transaction>(ttPAYMENT, naPublicKey, naSourceAccount, uSeq, saFee, uSourceTag);

	return tResult->setPayment(naPrivateKey, naDstAccountID, saAmount, saSendMax, spsPaths, bPartial, bLimit);
}

//
// WalletAdd
//

Transaction::pointer Transaction::setWalletAdd(
	const RippleAddress&				naPrivateKey,
	const STAmount&						saAmount,
	const RippleAddress&				naAuthKeyID,
	const RippleAddress&				naNewPubKey,
	const std::vector<unsigned char>&	vucSignature)
{
	mTransaction->setFieldAmount(sfAmount, saAmount);
	mTransaction->setFieldAccount(sfAuthorizedKey, naAuthKeyID);
	mTransaction->setFieldVL(sfPublicKey, naNewPubKey.getAccountPublic());
	mTransaction->setFieldVL(sfSignature, vucSignature);

	sign(naPrivateKey);

	return shared_from_this();
}

Transaction::pointer Transaction::sharedWalletAdd(
	const RippleAddress& naPublicKey, const RippleAddress& naPrivateKey,
	const RippleAddress&				naSourceAccount,
	uint32								uSeq,
	const STAmount&						saFee,
	uint32								uSourceTag,
	const STAmount&						saAmount,
	const RippleAddress&				naAuthKeyID,
	const RippleAddress&				naNewPubKey,
	const std::vector<unsigned char>&	vucSignature)
{
	pointer	tResult	= boost::make_shared<Transaction>(ttWALLET_ADD, naPublicKey, naSourceAccount, uSeq, saFee, uSourceTag);

	return tResult->setWalletAdd(naPrivateKey, saAmount, naAuthKeyID, naNewPubKey, vucSignature);
}

//
// Misc.
//

bool Transaction::checkSign() const
{
	assert(mFromPubKey.isValid());
	return mTransaction->checkSign(mFromPubKey);
}

void Transaction::setStatus(TransStatus ts, uint32 lseq)
{
	mStatus		= ts;
	mInLedger	= lseq;
}

void Transaction::saveTransaction(const Transaction::pointer& txn)
{
	txn->save();
}

bool Transaction::save()
{
	if ((mStatus == INVALID) || (mStatus == REMOVED)) return false;

	char status;
	switch (mStatus)
	{
	 case NEW:			status = TXN_SQL_NEW;		break;
	 case INCLUDED:		status = TXN_SQL_INCLUDED;	break;
	 case CONFLICTED:	status = TXN_SQL_CONFLICT;	break;
	 case COMMITTED:	status = TXN_SQL_VALIDATED;	break;
	 case HELD:			status = TXN_SQL_HELD;		break;
	 default:			status = TXN_SQL_UNKNOWN;
	}

	std::string exists = boost::str(boost::format("SELECT Status FROM Transactions WHERE TransID = '%s';")
		% mTransaction->getTransactionID().GetHex());

	Database *db = theApp->getTxnDB()->getDB();
	ScopedLock dbLock = theApp->getTxnDB()->getDBLock();
	if (SQL_EXISTS(db, exists)) return false;
	return
		db->executeSQL(mTransaction->getSQLInsertHeader() + mTransaction->getSQL(getLedger(), status) + ";");
}

Transaction::pointer Transaction::transactionFromSQL(Database* db, bool bValidate)
{
	Serializer rawTxn;
	std::string status;
	uint32 inLedger;

	int txSize = 2048;
	rawTxn.resize(txSize);

	db->getStr("Status", status);
	inLedger = db->getInt("LedgerSeq");
	txSize = db->getBinary("RawTxn", &*rawTxn.begin(), rawTxn.getLength());
	if (txSize > rawTxn.getLength())
	{
		rawTxn.resize(txSize);
		db->getBinary("RawTxn", &*rawTxn.begin(), rawTxn.getLength());
	}

	rawTxn.resize(txSize);

	SerializerIterator it(rawTxn);
	SerializedTransaction::pointer txn = boost::make_shared<SerializedTransaction>(boost::ref(it));
	Transaction::pointer tr = boost::make_shared<Transaction>(txn, bValidate);

	TransStatus st(INVALID);
	switch (status[0])
	{
	case TXN_SQL_NEW:			st = NEW;			break;
	case TXN_SQL_CONFLICT:		st = CONFLICTED;	break;
	case TXN_SQL_HELD:			st = HELD;			break;
	case TXN_SQL_VALIDATED:		st = COMMITTED;		break;
	case TXN_SQL_INCLUDED:		st = INCLUDED;		break;
	case TXN_SQL_UNKNOWN:							break;
	default: assert(false);
	}
	tr->setStatus(st);
	tr->setLedger(inLedger);
	return tr;
}

// DAVID: would you rather duplicate this code or keep the lock longer?
Transaction::pointer Transaction::transactionFromSQL(const std::string& sql)
{
	Serializer rawTxn;
	std::string status;
	uint32 inLedger;

	int txSize = 2048;
	rawTxn.resize(txSize);

	{
		ScopedLock sl(theApp->getTxnDB()->getDBLock());
		Database* db = theApp->getTxnDB()->getDB();

		if (!db->executeSQL(sql, true) || !db->startIterRows())
			return Transaction::pointer();

		db->getStr("Status", status);
		inLedger = db->getInt("LedgerSeq");
		txSize = db->getBinary("RawTxn", &*rawTxn.begin(), rawTxn.getLength());
		if (txSize > rawTxn.getLength())
		{
			rawTxn.resize(txSize);
			db->getBinary("RawTxn", &*rawTxn.begin(), rawTxn.getLength());
		}
		db->endIterRows();
	}
	rawTxn.resize(txSize);

	SerializerIterator it(rawTxn);
	SerializedTransaction::pointer txn = boost::make_shared<SerializedTransaction>(boost::ref(it));
	Transaction::pointer tr = boost::make_shared<Transaction>(txn, true);

	TransStatus st(INVALID);
	switch (status[0])
	{
		case TXN_SQL_NEW:			st = NEW;			break;
		case TXN_SQL_CONFLICT:		st = CONFLICTED;	break;
		case TXN_SQL_HELD:			st = HELD;			break;
		case TXN_SQL_VALIDATED:		st = COMMITTED;		break;
		case TXN_SQL_INCLUDED:		st = INCLUDED;		break;
		case TXN_SQL_UNKNOWN:							break;
		default: assert(false);
	}
	tr->setStatus(st);
	tr->setLedger(inLedger);
	return tr;
}

Transaction::pointer Transaction::load(const uint256& id)
{
	std::string sql = "SELECT LedgerSeq,Status,RawTxn FROM Transactions WHERE TransID='";
	sql.append(id.GetHex());
	sql.append("';");
	return transactionFromSQL(sql);
}

Transaction::pointer Transaction::findFrom(const RippleAddress& fromID, uint32 seq)
{
	std::string sql = "SELECT LedgerSeq,Status,RawTxn FROM Transactions WHERE FromID='";
	sql.append(fromID.humanAccountID());
	sql.append("' AND FromSeq='");
	sql.append(boost::lexical_cast<std::string>(seq));
	sql.append("';");
	return transactionFromSQL(sql);
}

bool Transaction::convertToTransactions(uint32 firstLedgerSeq, uint32 secondLedgerSeq,
	bool checkFirstTransactions, bool checkSecondTransactions, const SHAMap::SHAMapDiff& inMap,
	std::map<uint256, std::pair<Transaction::pointer, Transaction::pointer> >& outMap)
{ // convert a straight SHAMap payload difference to a transaction difference table
  // return value: true=ledgers are valid, false=a ledger is invalid
	SHAMap::SHAMapDiff::const_iterator it;
	for(it = inMap.begin(); it != inMap.end(); ++it)
	{
		const uint256& id = it->first;
		SHAMapItem::ref first = it->second.first;
		SHAMapItem::ref second = it->second.second;

		Transaction::pointer firstTrans, secondTrans;
		if (!!first)
		{ // transaction in our table
			firstTrans = sharedTransaction(first->getData(), checkFirstTransactions);
			if ((firstTrans->getStatus() == INVALID) || (firstTrans->getID() != id ))
			{
				firstTrans->setStatus(INVALID, firstLedgerSeq);
				return false;
			}
			else firstTrans->setStatus(INCLUDED, firstLedgerSeq);
		}

		if (!!second)
		{ // transaction in other table
			secondTrans = sharedTransaction(second->getData(), checkSecondTransactions);
			if ((secondTrans->getStatus() == INVALID) || (secondTrans->getID() != id))
			{
				secondTrans->setStatus(INVALID, secondLedgerSeq);
				return false;
			}
			else secondTrans->setStatus(INCLUDED, secondLedgerSeq);
		}
		assert(firstTrans || secondTrans);
		if(firstTrans && secondTrans && (firstTrans->getStatus() != INVALID) && (secondTrans->getStatus() != INVALID))
			return false; // one or the other SHAMap is structurally invalid or a miracle has happened

		outMap[id] = std::pair<Transaction::pointer, Transaction::pointer>(firstTrans, secondTrans);
	}
	return true;
}

Json::Value Transaction::getJson(int options) const
{
	Json::Value ret(mTransaction->getJson(0));

	if (mInLedger) ret["inLedger"]=mInLedger;

	switch (mStatus)
	{
		case NEW:			ret["status"] = "new";			break;
		case INVALID:		ret["status"] = "invalid";		break;
		case INCLUDED:		ret["status"] = "included";		break;
		case CONFLICTED:	ret["status"] = "conflicted";	break;
		case COMMITTED:		ret["status"] = "committed";	break;
		case HELD:			ret["status"] = "held";			break;
		case REMOVED:		ret["status"] = "removed";		break;
		case OBSOLETE:		ret["status"] = "obsolete";		break;
		case INCOMPLETE:	ret["status"] = "incomplete";	break;
		default:			ret["status"] = "unknown";		break;
	}

	return ret;
}

//
// Obsolete
//

static bool isHex(char j)
{
	if ((j >= '0') && (j <= '9')) return true;
	if ((j >= 'A') && (j <= 'F')) return true;
	if ((j >= 'a') && (j <= 'f')) return true;
	return false;
}

bool Transaction::isHexTxID(const std::string& txid)
{
	if (txid.size() != 64) return false;
	for (int i = 0; i < 64; ++i)
		if (!isHex(txid[i])) return false;
	return true;
}

// vim:ts=4
