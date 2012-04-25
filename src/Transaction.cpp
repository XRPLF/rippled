#include <cassert>

#include "boost/lexical_cast.hpp"
#include "boost/make_shared.hpp"
#include "boost/ref.hpp"

#include "Application.h"
#include "Transaction.h"
#include "Wallet.h"
#include "BitcoinUtil.h"
#include "Serializer.h"
#include "SerializedTransaction.h"

Transaction::Transaction(LocalAccount::pointer fromLocalAccount, const NewcoinAddress& toAccount, uint64 amount,
	uint32 ident, uint32 ledger) : mInLedger(0), mStatus(NEW)
{
	AccountState::pointer accountState = fromLocalAccount->getAccountState();
	if (!accountState) throw std::runtime_error("transaction on non-existent account");

	mAccountFrom = fromLocalAccount->getAddress();

	mTransaction = boost::make_shared<SerializedTransaction>(ttMAKE_PAYMENT);

	mFromPubKey = fromLocalAccount->getPublicKey();
	assert(mFromPubKey);
	mTransaction->setSigningPubKey(mFromPubKey->GetPubKey());

	mTransaction->setSequence(accountState->getSeq());
	assert(mTransaction->getSequence() != 0);

	mTransaction->setTransactionFee(100); // for now

	mTransaction->setITFieldAccount(sfDestination, toAccount);
	mTransaction->setITFieldU64(sfAmount, amount);
	if (ledger != 0)
	{
		mTransaction->makeITFieldPresent(sfTargetLedger);
		mTransaction->setITFieldU32(sfTargetLedger, ledger);
	}
	if (ident != 0)
	{
		mTransaction->makeITFieldPresent(sfSourceTag);
		mTransaction->setITFieldU32(sfSourceTag, ident);
	}

	assert(mFromPubKey);
	if (!sign(fromLocalAccount))
	{
#ifdef DEBUG
		std::cerr << "Unable to sign transaction" << std::endl;
#endif
		mStatus = INCOMPLETE;
	}
}

Transaction::Transaction(SerializedTransaction::pointer sit, bool validate)
	: mInLedger(0), mStatus(INVALID), mTransaction(sit)
{
	std::vector<unsigned char> pubKey;

	try
	{
		pubKey = mTransaction->peekSigningPubKey();
		mTransactionID = mTransaction->getTransactionID();
		mAccountFrom = mTransaction->getSourceAccount();
	}
	catch(...)
	{
		return;
	}

	mFromPubKey = boost::make_shared<CKey>();
	if (!mFromPubKey->SetPubKey(pubKey)) return;
	mFromPubKey = theApp->getPubKeyCache().store(mAccountFrom, mFromPubKey);

	if (!validate || checkSign())
		mStatus = NEW;
}

Transaction::Transaction(const std::vector<unsigned char>& raw, bool validate) : mInLedger(0), mStatus(INVALID)
{
	uint160	toAccountID;
	uint160	fromAccountID;
	std::vector<unsigned char> pubKey;

	try
	{
		Serializer s(raw);
		SerializerIterator sit(s);
		mTransaction = boost::make_shared<SerializedTransaction>(boost::ref(sit), -1);

		mFromPubKey = boost::make_shared<CKey>();
		if (!mFromPubKey->SetPubKey(pubKey)) return;
		mAccountFrom.setAccountPublic(pubKey);
		mFromPubKey = theApp->getPubKeyCache().store(mAccountFrom, mFromPubKey);
		if (!mFromPubKey->SetPubKey(pubKey)) return;
		mAccountFrom.setAccountPublic(pubKey);
		mFromPubKey = theApp->getPubKeyCache().store(mAccountFrom, mFromPubKey);
	}
	catch (...)
	{
		return;
	}
	if (!validate || checkSign())
		mStatus = NEW;
}

Transaction::Transaction(const NewcoinAddress& fromID, const NewcoinAddress& toID,
	CKey::pointer pubKey, uint64 amount, uint64 fee, uint32 fromSeq, uint32 fromLedger,
	uint32 ident, const std::vector<unsigned char>& signature, uint32 ledgerSeq, TransStatus st) :
	mAccountFrom(fromID), mFromPubKey(pubKey), mInLedger(ledgerSeq), mStatus(st)
{
	mTransaction = boost::make_shared<SerializedTransaction>(ttMAKE_PAYMENT);
	mTransaction->setSignature(signature);
	mTransaction->setTransactionFee(fee);
	mTransaction->setSigningPubKey(pubKey->GetPubKey());
	mTransaction->setSequence(fromSeq);
	if (fromLedger != 0)
	{
		mTransaction->makeITFieldPresent(sfTargetLedger);
		mTransaction->setITFieldU32(sfTargetLedger, fromLedger);
	}
	if (ident != 0)
	{
		mTransaction->makeITFieldPresent(sfSourceTag);
		mTransaction->setITFieldU32(sfSourceTag, ident);
	}
	mTransaction->setValueFieldU64(sfAmount, amount);
	mTransaction->setValueFieldAccount(sfDestination, toID.getAccountID());
	updateID();
}

bool Transaction::sign(LocalAccount::pointer fromLocalAccount)
{
	CKey::pointer privateKey = fromLocalAccount->getPrivateKey();
	if(!privateKey)
	{
#ifdef DEBUG
		std::cerr << "No private key for signing" << std::endl;
#endif
		return false;
	}

	if( (mTransaction->getITFieldU64(sfAmount)==0) )
	{
#ifdef DEBUG
		std::cerr << "Bad amount or destination" << std::endl;
#endif
		assert(false);
		return false;
	}

	if(mAccountFrom.getAccountID()!=fromLocalAccount->getAddress().getAccountID())
	{
#ifdef DEBUG
		std::cerr << "Source mismatch" << std::endl;
#endif
		assert(false);
		return false;
	}

	if(!getSTransaction()->sign(*privateKey))
	{
#ifdef DEBUG
		std::cerr << "Failed to make signature" << std::endl;
#endif
		assert(false);
		return false;
	}
	updateID();
	return true;
}

bool Transaction::checkSign() const
{
	assert(mFromPubKey);
	return mTransaction->checkSign(*mFromPubKey);
}

void Transaction::setStatus(TransStatus ts, uint32 lseq)
{
	mStatus=ts;
	mInLedger=lseq;
}

void Transaction::saveTransaction(Transaction::pointer txn)
{
	txn->save();
}

bool Transaction::save() const
{ // This code needs to be fixed to support new-style transactions - FIXME
	if((mStatus==INVALID)||(mStatus==REMOVED)) return false;

	std::string sql="INSERT INTO Transactions "
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
	Database* db=theApp->getTxnDB()->getDB();
	return db->executeSQL(sql.c_str());
}

Transaction::pointer Transaction::transactionFromSQL(const std::string& sql)
{ // This code needs to be fixed to support new-style transactions - FIXME
	std::vector<unsigned char> rawTxn;
	std::string status;

	rawTxn.reserve(2048);
	if(1)
	{
		ScopedLock sl(theApp->getTxnDB()->getDBLock());
		Database* db=theApp->getTxnDB()->getDB();

		if(!db->executeSQL(sql.c_str(), true) || !db->startIterRows() || !db->getNextRow())
			return Transaction::pointer();

		db->getStr("Status", status);
		int txSize=db->getBinary("RawTxn", &(rawTxn.front()), rawTxn.size());
		rawTxn.resize(txSize);
		if(txSize>rawTxn.size()) db->getBinary("RawTxn", &(rawTxn.front()), rawTxn.size());
		db->endIterRows();
	}

	Serializer s(rawTxn);
	SerializerIterator it(s);
	SerializedTransaction::pointer txn=boost::make_shared<SerializedTransaction>(boost::ref(it), -1);
	Transaction::pointer tr=boost::make_shared<Transaction>(txn, true);

	TransStatus st(INVALID);
	switch(status[0])
	{
		case 'N': st=NEW; break;
		case 'A': st=INCLUDED; break;
		case 'C': st=CONFLICTED; break;
		case 'D': st=COMMITTED; break;
		case 'H': st=HELD; break;
	}
	tr->setStatus(st);

	return tr;

}

Transaction::pointer Transaction::load(const uint256& id)
{
	std::string sql="SELECT Status,RawTxn FROM Transactions WHERE TransID='";
	sql.append(id.GetHex());
	sql.append("';");
	return transactionFromSQL(sql);
}

Transaction::pointer Transaction::findFrom(const NewcoinAddress& fromID, uint32 seq)
{
	std::string sql="SELECT Status,RawTxn FROM Transactions WHERE FromID='";
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
	for(it=inMap.begin(); it!=inMap.end(); ++it)
	{
		const uint256& id=it->first;
		const SHAMapItem::pointer& first=it->second.first;
		const SHAMapItem::pointer& second=it->second.second;
	
		Transaction::pointer firstTrans, secondTrans;	
		if(!!first)
		{ // transaction in our table
			Serializer s(first->getData());
			SerializerIterator sit(s);
			firstTrans=boost::make_shared<Transaction>(first->getData(), checkFirstTransactions);
			if( (firstTrans->getStatus()==INVALID) || (firstTrans->getID()!=id) )
			{
				firstTrans->setStatus(INVALID, firstLedgerSeq);
				return false;
			}
			else firstTrans->setStatus(INCLUDED, firstLedgerSeq);
		}

		if(!!second)
		{ // transaction in other table
			secondTrans=boost::make_shared<Transaction>(second->getData(), checkSecondTransactions);
			if( (secondTrans->getStatus()==INVALID) || (secondTrans->getID()!=id) )
			{
				secondTrans->setStatus(INVALID, secondLedgerSeq);
				return false;
			}
			else secondTrans->setStatus(INCLUDED, secondLedgerSeq);
		}
		assert(firstTrans || secondTrans);
		if(firstTrans && secondTrans && (firstTrans->getStatus()!=INVALID) && (secondTrans->getStatus()!=INVALID))
			return false; // one or the other SHAMap is structurally invalid or a miracle has happened

		outMap[id]=std::pair<Transaction::pointer, Transaction::pointer>(firstTrans, secondTrans);
	}
	return true;
}

static bool isHex(char j)
{
	if((j>='0') && (j<='9')) return true;
	if((j>='A') && (j<='F')) return true;
	if((j>='a') && (j<='f')) return true;
	return false;
}
						
bool Transaction::isHexTxID(const std::string& txid)
{
	if(txid.size()!=64) return false;
	for(int i=0; i<64; i++)
		if(!isHex(txid[i])) return false;
	return true;
}

Json::Value Transaction::getJson(bool decorate, bool paid, bool credited) const
{
	Json::Value ret(mTransaction->getJson(0));

	if(mInLedger) ret["InLedger"]=mInLedger;
	if(paid) ret["Paid"]=true;

	switch(mStatus)
	{
		case NEW: ret["Status"]="new"; break;
		case INVALID: ret["Status"]="invalid"; break;
		case INCLUDED: ret["Status"]="included"; break;
		case CONFLICTED: ret["Status"]="conflicted"; break;
		case COMMITTED: ret["Status"]="committed"; break;
		case HELD: ret["Status"]="held"; break;
		case REMOVED: ret["Status"]="removed"; break;
		case OBSOLETE: ret["Status"]="obsolete"; break;
		case INCOMPLETE: ret["Status"]="incomplete"; break;
		default: ret["Status"]="unknown";
	}

#if 0
	if(decorate)
	{
		LocalAccount::pointer lac=theApp->getWallet().getLocalAccount(mAccountFrom);
		if(!!lac) source=lac->getJson();
		lac=theApp->getWallet().getLocalAccount(mAccountTo);
		if(!!lac) destination=lac->getJson();
	}
#endif

	return ret;
}
