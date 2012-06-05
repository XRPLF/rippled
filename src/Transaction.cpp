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

Transaction::Transaction(const SerializedTransaction::pointer sit, bool bValidate)
	: mInLedger(0), mStatus(INVALID), mTransaction(sit)
{
	try
	{
		mFromPubKey.setAccountPublic(mTransaction->peekSigningPubKey());
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
	const NewcoinAddress&	naPublicKey,
	const NewcoinAddress&	naSourceAccount,
	uint32					uSeq,
	const STAmount&			saFee,
	uint32					uSourceTag) :
	mStatus(NEW)
{
	mAccountFrom	= naSourceAccount;
	mFromPubKey		= naPublicKey;
	assert(mFromPubKey.isValid());

	mTransaction	= boost::make_shared<SerializedTransaction>(ttKind);

	Log(lsINFO) << str(boost::format("Transaction: account: %s") % naSourceAccount.humanAccountID());
	Log(lsINFO) << str(boost::format("Transaction: mAccountFrom: %s") % mAccountFrom.humanAccountID());
	mTransaction->setSigningPubKey(mFromPubKey);
	mTransaction->setSourceAccount(mAccountFrom);
	mTransaction->setSequence(uSeq);
	mTransaction->setTransactionFee(saFee);

	if (uSourceTag)
	{
		mTransaction->makeITFieldPresent(sfSourceTag);
		mTransaction->setITFieldU32(sfSourceTag, uSourceTag);
	}
}

bool Transaction::sign(const NewcoinAddress& naAccountPrivate)
{
	bool	bResult	= true;

	if (!naAccountPrivate.isValid())
	{
		Log(lsWARNING) << "No private key for signing";
		bResult	= false;
	}
	else if (!getSTransaction()->sign(naAccountPrivate))
	{
		Log(lsWARNING) << "Failed to make signature";
		assert(false);
		bResult	= false;
	}

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
	const NewcoinAddress& naPrivateKey,
	bool								bUnsetEmailHash,
	const uint128&						uEmailHash,
	bool								bUnsetWalletLocator,
	const uint256&						uWalletLocator,
	const NewcoinAddress&				naMessagePublic)
{
	mTransaction->setITFieldU32(sfFlags,
		  (bUnsetEmailHash ? tfUnsetEmailHash : 0)
		| (bUnsetWalletLocator ? tfUnsetWalletLocator : 0));

	if (!bUnsetEmailHash && !!uEmailHash)
		mTransaction->setITFieldH128(sfEmailHash, uEmailHash);

	if (!bUnsetWalletLocator && !!uWalletLocator)
		mTransaction->setITFieldH256(sfWalletLocator, uWalletLocator);

	if (naMessagePublic.isValid())
		mTransaction->setITFieldVL(sfMessageKey, naMessagePublic.getAccountPublic());

	sign(naPrivateKey);

	return shared_from_this();
}

Transaction::pointer Transaction::sharedAccountSet(
	const NewcoinAddress& naPublicKey, const NewcoinAddress& naPrivateKey,
	const NewcoinAddress& naSourceAccount,
	uint32								uSeq,
	const STAmount&						saFee,
	uint32								uSourceTag,
	bool								bUnsetEmailHash,
	const uint128&						uEmailHash,
	bool								bUnsetWalletLocator,
	const uint256&						uWalletLocator,
	const NewcoinAddress&				naMessagePublic)
{
	pointer	tResult	= boost::make_shared<Transaction>(ttACCOUNT_SET, naPublicKey, naSourceAccount, uSeq, saFee, uSourceTag);

	return tResult->setAccountSet(naPrivateKey, bUnsetEmailHash, uEmailHash, bUnsetWalletLocator, uWalletLocator, naMessagePublic);
}

//
// Claim
//

Transaction::pointer Transaction::setClaim(
	const NewcoinAddress& naPrivateKey,
	const std::vector<unsigned char>& vucGenerator,
	const std::vector<unsigned char>& vucPubKey,
	const std::vector<unsigned char>& vucSignature)
{
	mTransaction->setITFieldVL(sfGenerator, vucGenerator);
	mTransaction->setITFieldVL(sfPubKey, vucPubKey);
	mTransaction->setITFieldVL(sfSignature, vucSignature);

	sign(naPrivateKey);

	return shared_from_this();
}

Transaction::pointer Transaction::sharedClaim(
	const NewcoinAddress& naPublicKey, const NewcoinAddress& naPrivateKey,
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
	const NewcoinAddress&	naPrivateKey,
	const NewcoinAddress&	naCreateAccountID,
	const STAmount&			saFund)
{
	mTransaction->setITFieldU32(sfFlags, tfCreateAccount);
	mTransaction->setITFieldAccount(sfDestination, naCreateAccountID);
	mTransaction->setITFieldAmount(sfAmount, saFund);

	sign(naPrivateKey);

	return shared_from_this();
}

Transaction::pointer Transaction::sharedCreate(
	const NewcoinAddress& naPublicKey, const NewcoinAddress& naPrivateKey,
	const NewcoinAddress& naSourceAccount,
	uint32					uSeq,
	const STAmount&			saFee,
	uint32					uSourceTag,
	const NewcoinAddress&	naCreateAccountID,
	const STAmount&			saFund)
{
	pointer	tResult	= boost::make_shared<Transaction>(ttPAYMENT, naPublicKey, naSourceAccount, uSeq, saFee, uSourceTag);

	return tResult->setCreate(naPrivateKey, naCreateAccountID, saFund);
}

//
// CreditSet
//

Transaction::pointer Transaction::setCreditSet(
	const NewcoinAddress&	naPrivateKey,
	const NewcoinAddress&	naDstAccountID,
	const STAmount&			saLimitAmount,
	uint32					uAcceptRate)
{
	mTransaction->setITFieldAccount(sfDestination, naDstAccountID);
	mTransaction->setITFieldAmount(sfLimitAmount, saLimitAmount);
	if (uAcceptRate)
		mTransaction->setITFieldU32(sfAcceptRate, uAcceptRate);

	sign(naPrivateKey);

	return shared_from_this();
}

Transaction::pointer Transaction::sharedCreditSet(
	const NewcoinAddress& naPublicKey, const NewcoinAddress& naPrivateKey,
	const NewcoinAddress&	naSourceAccount,
	uint32					uSeq,
	const STAmount&			saFee,
	uint32					uSourceTag,
	const NewcoinAddress&	naDstAccountID,
	const STAmount&			saLimitAmount,
	uint32					uAcceptRate)
{
	pointer	tResult	= boost::make_shared<Transaction>(ttCREDIT_SET, naPublicKey, naSourceAccount, uSeq, saFee, uSourceTag);

	return tResult->setCreditSet(naPrivateKey, naDstAccountID, saLimitAmount, uAcceptRate);
}

//
// NicknameSet
//

Transaction::pointer Transaction::setNicknameSet(
	const NewcoinAddress&				naPrivateKey,
	const uint256&						uNickname,
	bool								bSetOffer,
	const STAmount&						saMinimumOffer)
{
	mTransaction->setITFieldH256(sfNickname, uNickname);

	// XXX Make sure field is present even for 0!
	if (bSetOffer)
		mTransaction->setITFieldAmount(sfMinimumOffer, saMinimumOffer);

	sign(naPrivateKey);

	return shared_from_this();
}

// --> bSetOffer: true, change offer
// --> saMinimumOffer: 0 to remove.
Transaction::pointer Transaction::sharedNicknameSet(
	const NewcoinAddress& naPublicKey, const NewcoinAddress& naPrivateKey,
	const NewcoinAddress&				naSourceAccount,
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
// PasswordFund
//

Transaction::pointer Transaction::setPasswordFund(
	const NewcoinAddress&	naPrivateKey,
	const NewcoinAddress&	naDstAccountID)
{
	mTransaction->setITFieldAccount(sfDestination, naDstAccountID);

	sign(naPrivateKey);

	return shared_from_this();
}

Transaction::pointer Transaction::sharedPasswordFund(
	const NewcoinAddress& naPublicKey, const NewcoinAddress& naPrivateKey,
	const NewcoinAddress&	naSourceAccount,
	uint32					uSeq,
	const STAmount&			saFee,
	uint32					uSourceTag,
	const NewcoinAddress&	naDstAccountID)
{
	pointer	tResult	= boost::make_shared<Transaction>(ttPASSWORD_FUND, naPublicKey, naSourceAccount, uSeq, saFee, uSourceTag);

	return tResult->setPasswordFund(naPrivateKey, naDstAccountID);
}

//
// PasswordSet
//

Transaction::pointer Transaction::setPasswordSet(
	const NewcoinAddress& naPrivateKey,
	const NewcoinAddress&				naAuthKeyID,
	const std::vector<unsigned char>&	vucGenerator,
	const std::vector<unsigned char>&	vucPubKey,
	const std::vector<unsigned char>&	vucSignature)
{
	mTransaction->setITFieldAccount(sfAuthorizedKey, naAuthKeyID);
	mTransaction->setITFieldVL(sfGenerator, vucGenerator);
	mTransaction->setITFieldVL(sfPubKey, vucPubKey);
	mTransaction->setITFieldVL(sfSignature, vucSignature);

	sign(naPrivateKey);

	return shared_from_this();
}

Transaction::pointer Transaction::sharedPasswordSet(
	const NewcoinAddress& naPublicKey, const NewcoinAddress& naPrivateKey,
	uint32								uSourceTag,
	const NewcoinAddress&				naAuthKeyID,
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
	const NewcoinAddress&	naPrivateKey,
	const NewcoinAddress&	naDstAccountID,
	const STAmount&			saAmount,
	const STAmount&			saSendMax,
	const STPathSet&		spPaths)
{
	mTransaction->setITFieldAccount(sfDestination, naDstAccountID);
	mTransaction->setITFieldAmount(sfAmount, saAmount);

	if (saAmount != saSendMax)
	{
		mTransaction->setITFieldAmount(sfSendMax, saSendMax);
	}

	if (spPaths.getPathCount())
	{
		mTransaction->setITFieldPathSet(sfPaths, spPaths);
	}

	sign(naPrivateKey);

	return shared_from_this();
}

Transaction::pointer Transaction::sharedPayment(
	const NewcoinAddress& naPublicKey, const NewcoinAddress& naPrivateKey,
	const NewcoinAddress&	naSourceAccount,
	uint32					uSeq,
	const STAmount&			saFee,
	uint32					uSourceTag,
	const NewcoinAddress&	naDstAccountID,
	const STAmount&			saAmount,
	const STAmount&			saSendMax,
	const STPathSet&		saPaths)
{
	pointer	tResult	= boost::make_shared<Transaction>(ttPAYMENT, naPublicKey, naSourceAccount, uSeq, saFee, uSourceTag);

	return tResult->setPayment(naPrivateKey, naDstAccountID, saAmount, saSendMax, saPaths);
}

//
// TransitSet
//

Transaction::pointer Transaction::setTransitSet(
	const NewcoinAddress&	naPrivateKey,
	uint32					uTransitRate,
	uint32					uTransitStart,
	uint32					uTransitExpire)
{
	if (uTransitRate)
		mTransaction->setITFieldU32(sfTransitRate, uTransitRate);
	if (uTransitStart)
		mTransaction->setITFieldU32(sfTransitStart, uTransitStart);
	if (uTransitExpire)
		mTransaction->setITFieldU32(sfTransitExpire, uTransitExpire);

	sign(naPrivateKey);

	return shared_from_this();
}

Transaction::pointer Transaction::sharedTransitSet(
	const NewcoinAddress& naPublicKey, const NewcoinAddress& naPrivateKey,
	const NewcoinAddress&	naSourceAccount,
	uint32					uSeq,
	const STAmount&			saFee,
	uint32					uSourceTag,
	uint32					uTransitRate,
	uint32					uTransitStart,
	uint32					uTransitExpire)
{
	pointer	tResult	= boost::make_shared<Transaction>(ttTRANSIT_SET, naPublicKey, naSourceAccount, uSeq, saFee, uSourceTag);

	return tResult->setTransitSet(naPrivateKey, uTransitRate, uTransitStart, uTransitExpire);
}

//
// WalletAdd
//

Transaction::pointer Transaction::setWalletAdd(
	const NewcoinAddress&				naPrivateKey,
	const STAmount&						saAmount,
	const NewcoinAddress&				naAuthKeyID,
	const NewcoinAddress&				naNewPubKey,
	const std::vector<unsigned char>&	vucSignature)
{
	mTransaction->setITFieldAmount(sfAmount, saAmount);
	mTransaction->setITFieldAccount(sfAuthorizedKey, naAuthKeyID);
	mTransaction->setITFieldVL(sfPubKey, naNewPubKey.getAccountPublic());
	mTransaction->setITFieldVL(sfSignature, vucSignature);

	sign(naPrivateKey);

	return shared_from_this();
}

Transaction::pointer Transaction::sharedWalletAdd(
	const NewcoinAddress& naPublicKey, const NewcoinAddress& naPrivateKey,
	const NewcoinAddress&				naSourceAccount,
	uint32								uSeq,
	const STAmount&						saFee,
	uint32								uSourceTag,
	const STAmount&						saAmount,
	const NewcoinAddress&				naAuthKeyID,
	const NewcoinAddress&				naNewPubKey,
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

void Transaction::saveTransaction(Transaction::pointer txn)
{
	txn->save();
}

bool Transaction::save() const
{ // This code needs to be fixed to support new-style transactions - FIXME
#if 0
	// Identify minimums fields to write for now.
	// Also maybe write effected accounts for use later.
	if ((mStatus == INVALID) || (mStatus == REMOVED)) return false;

	std::string sql = "INSERT INTO Transactions "
		"(TransID,TransType,FromAcct,FromSeq,OtherAcct,Amount,FirstSeen,CommitSeq,Status,RawTxn)"
		" VALUES ('";
	sql.append(mTransactionID.GetHex());
	sql.append("','");
	sql.append(mTransaction->getTransactionType());
	sql.append("','");
	sql.append(mAccountFrom.humanAccountID());
	sql.append("','");
	sql.append(boost::lexical_cast<std::string>(mTransaction->getSequence()));
	sql.append("','");
	sql.append(mTransaction->getITFieldString(sfDestination));
	sql.append("','");
	sql.append(mTransaction->getITFieldString(sfAmount));
	sql.append("',now(),'");
	sql.append(boost::lexical_cast<std::string>(mInLedger));
	switch(mStatus)
	{
	 case NEW: sql.append("','N',"); break;
	 case INCLUDED: sql.append("','A',"); break;
	 case CONFLICTED: sql.append("','C',"); break;
	 case COMMITTED: sql.append("','D',"); break;
	 case HELD: sql.append("','H',"); break;
	 default: sql.append("','U',"); break;
	}

	Serializer s;
	mTransaction->getTransaction(s, false);

	std::string rawTxn;
	theApp->getTxnDB()->getDB()->escape(static_cast<const unsigned char *>(s.getDataPtr()), s.getLength(), rawTxn);
	sql.append(rawTxn);
	sql.append(");");

	ScopedLock sl(theApp->getTxnDB()->getDBLock());
	Database* db = theApp->getTxnDB()->getDB();
	return db->executeSQL(sql);
#endif
	return true;
}

Transaction::pointer Transaction::transactionFromSQL(const std::string& sql)
{ // This code needs to be fixed to support new-style transactions - FIXME
	std::vector<unsigned char> rawTxn;
	std::string status;

	rawTxn.reserve(2048);
	{
		ScopedLock sl(theApp->getTxnDB()->getDBLock());
		Database* db = theApp->getTxnDB()->getDB();

		if (!db->executeSQL(sql, true) || !db->startIterRows())
			return Transaction::pointer();

		db->getStr("Status", status);
		int txSize = db->getBinary("RawTxn", &(rawTxn.front()), rawTxn.size());
		rawTxn.resize(txSize);
		if (txSize>rawTxn.size()) db->getBinary("RawTxn", &(rawTxn.front()), rawTxn.size());
		db->endIterRows();
	}

	Serializer s(rawTxn);
	SerializerIterator it(s);
	SerializedTransaction::pointer txn = boost::make_shared<SerializedTransaction>(boost::ref(it));
	Transaction::pointer tr = boost::make_shared<Transaction>(txn, true);

	TransStatus st(INVALID);
	switch (status[0])
	{
		case 'N': st = NEW; break;
		case 'A': st = INCLUDED; break;
		case 'C': st = CONFLICTED; break;
		case 'D': st = COMMITTED; break;
		case 'H': st = HELD; break;
	}
	tr->setStatus(st);

	return tr;
}

Transaction::pointer Transaction::load(const uint256& id)
{
	std::string sql = "SELECT Status,RawTxn FROM Transactions WHERE TransID='";
	sql.append(id.GetHex());
	sql.append("';");
	return transactionFromSQL(sql);
}

Transaction::pointer Transaction::findFrom(const NewcoinAddress& fromID, uint32 seq)
{
	std::string sql = "SELECT Status,RawTxn FROM Transactions WHERE FromID='";
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
	std::map<uint256, std::pair<SHAMapItem::pointer, SHAMapItem::pointer> >::const_iterator it;
	for(it = inMap.begin(); it != inMap.end(); ++it)
	{
		const uint256& id = it->first;
		const SHAMapItem::pointer& first = it->second.first;
		const SHAMapItem::pointer& second = it->second.second;

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

Json::Value Transaction::getJson(bool decorate, bool paid, bool credited) const
{
	Json::Value ret(mTransaction->getJson(0));

	if (mInLedger) ret["InLedger"]=mInLedger;
	if (paid) ret["Paid"]=true;

	switch(mStatus)
	{
		case NEW: ret["Status"] = "new"; break;
		case INVALID: ret["Status"] = "invalid"; break;
		case INCLUDED: ret["Status"] = "included"; break;
		case CONFLICTED: ret["Status"] = "conflicted"; break;
		case COMMITTED: ret["Status"] = "committed"; break;
		case HELD: ret["Status"] = "held"; break;
		case REMOVED: ret["Status"] = "removed"; break;
		case OBSOLETE: ret["Status"] = "obsolete"; break;
		case INCOMPLETE: ret["Status"] = "incomplete"; break;
		default: ret["Status"] = "unknown";
	}

#if 0
	if(decorate)
	{
		LocalAccount::pointer lac = theApp->getWallet().getLocalAccount(mAccountFrom);
		if (!!lac) source = lac->getJson();
		lac = theApp->getWallet().getLocalAccount(mAccountTo);
		if (!!lac) destination = lac->getJson();
	}
#endif

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
