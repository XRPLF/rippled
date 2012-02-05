#include <cassert>

#include "boost/lexical_cast.hpp"
#include "boost/make_shared.hpp"

#include "Application.h"
#include "Transaction.h"
#include "Wallet.h"
#include "BitcoinUtil.h"
#include "BinaryFormats.h"

using namespace std;

Transaction::Transaction() : mTransactionID(0), mAccountFrom(0), mAccountTo(0),
	mAmount(0), mFee(0), mFromAccountSeq(0), mSourceLedger(0), mIdent(0),
	mInLedger(0), mStatus(INVALID)
{
}

Transaction::Transaction(LocalAccount::pointer fromLocalAccount, const uint160& toAccount, uint64 amount,
	uint32 ident, uint32 ledger) :
		mAccountTo(toAccount), mAmount(amount), mSourceLedger(ledger), mIdent(ident), mInLedger(0), mStatus(NEW)
{
	mAccountFrom=fromLocalAccount->getAddress();
	mFromPubKey=fromLocalAccount->getPublicKey();
	assert(mFromPubKey);
	mFromAccountSeq=fromLocalAccount->getTxnSeq();

#ifdef DEBUG
		std::cerr << "Construct local Txn" << std::endl;
		std::cerr << "ledger(" << ledger << "), fromseq(" << mFromAccountSeq << ")" << std::endl;
#endif

	if(!mFromAccountSeq)
	{
#ifdef DEBUG
		std::cerr << "Bad source account sequence" << std::endl;
		assert(false);
#endif
		mStatus=INCOMPLETE;
	}
	assert(mFromPubKey);
	updateFee();
	if(!sign(fromLocalAccount))
	{
#ifdef DEBUG
		std::cerr << "Unable to sign transaction" << std::endl;
#endif
		mStatus=INCOMPLETE;
	}
}

Transaction::Transaction(const std::vector<unsigned char> &t, bool validate) : mStatus(INVALID)
{
	Serializer s(t);
	if(s.getLength()<BTxSize) { assert(false); return; }
	if(!s.get160(mAccountTo, BTxPDestAcct)) { assert(false); return; }
	if(!s.get64(mAmount, BTxPAmount)) { assert(false); return; }
	if(!s.get32(mFromAccountSeq, BTxPSASeq)) { assert(false); return; }
	if(!s.get32(mSourceLedger, BTxPSLIdx)) { assert(false); return; }
	if(!s.get32(mIdent, BTxPSTag)) { assert(false); return; }
	if(!s.getRaw(mSignature, BTxPSig, BTxLSig)) { assert(false); return; }

	std::vector<unsigned char> pubKey;
	if(!s.getRaw(pubKey, BTxPSPubK, BTxLSPubK)) { assert(false); return; }
	mFromPubKey=boost::make_shared<CKey>();
	if(!mFromPubKey->SetPubKey(pubKey)) return;
	mAccountFrom=Hash160(pubKey);
	mFromPubKey=theApp->getPubKeyCache().store(mAccountFrom, mFromPubKey);

	updateID();
	updateFee();

	if(!validate || checkSign())
		mStatus=NEW;
}

bool Transaction::sign(LocalAccount::pointer fromLocalAccount)
{
	CKey::pointer privateKey=fromLocalAccount->getPrivateKey();
	if(!privateKey)
	{
#ifdef DEBUG
		std::cerr << "No private key for signing" << std::endl;
#endif
		return false;
	}

	if( (mAmount==0) || (mSourceLedger==0) || (mAccountTo==0) )
	{
#ifdef DEBUG
		std::cerr << "Bad amount, source ledger, or destination" << std::endl;
#endif
		assert(false);
		return false;
	}
	if(mAccountFrom!=fromLocalAccount->getAddress())
	{
#ifdef DEBUG
		std::cerr << "Source mismatch" << std::endl;
#endif
		assert(false);
		return false;
	}
	if(!getRaw(true)->makeSignature(mSignature, *privateKey))
	{
#ifdef DEBUG
		std::cerr << "Failed to make signature" << std::endl;
#endif
		assert(false);
		return false;
	}
	assert(mSignature.size()==72);
	updateID();
	return true;
}

void Transaction::updateFee()
{ // for now, all transactions have a 1,000 unit fee
	mFee=1000;
}

bool Transaction::checkSign() const
{
	assert(mFromPubKey);
	Serializer::pointer signBuf=getRaw(true);
	return getRaw(true)->checkSignature(mSignature, *mFromPubKey);
}

Serializer::pointer Transaction::getRaw(bool prefix) const
{
	Serializer::pointer ret=boost::make_shared<Serializer>(77);
	if(prefix) ret->add32(0x54584e00u);
	ret->add160(mAccountTo);
	ret->add64(mAmount);
	ret->add32(mFromAccountSeq);
	ret->add32(mSourceLedger);
	ret->add32(mIdent);
	ret->addRaw(mFromPubKey->GetPubKey());
	assert( (prefix&&(ret->getLength()==77)) || (!prefix&&(ret->getLength()==73)) );
	return ret;
}

Serializer::pointer Transaction::getSigned() const
{
	Serializer::pointer ret(getRaw(false));
	ret->addRaw(mSignature);
	assert(ret->getLength()==BTxSize);
	return ret;
}

void Transaction::updateID()
{
	mTransactionID=getSigned()->getSHA512Half();
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
{
	if((mStatus==INVALID)||(mStatus==REMOVED)) return false;

	std::string sql="INSERT INTO Transactions "
		"(TransID,FromAcct,FromSeq,FromLedger,Identifier,ToAcct,Amount,Fee,FirstSeen,CommitSeq,Status, Signature)"
		" VALUES ('";
	sql.append(mTransactionID.GetHex());
	sql.append("','");
	sql.append(mAccountFrom.GetHex());
	sql.append("','");
	sql.append(boost::lexical_cast<std::string>(mFromAccountSeq));
	sql.append("','");
	sql.append(boost::lexical_cast<std::string>(mSourceLedger));
	sql.append("','");
	sql.append(boost::lexical_cast<std::string>(mIdent));
	sql.append("','");
	sql.append(mAccountTo.GetHex());
	sql.append("','");
	sql.append(boost::lexical_cast<std::string>(mAmount));
	sql.append("','");
	sql.append(boost::lexical_cast<std::string>(mFee));
	sql.append("',now(),'");
	sql.append(boost::lexical_cast<std::string>(mInLedger)); switch(mStatus)
	{
	 case NEW: sql.append("','N',"); break;
	 case INCLUDED: sql.append("','A',"); break;
	 case CONFLICTED: sql.append("','C',"); break;
	 case COMMITTED: sql.append("','D',"); break;
	 case HELD: sql.append("','H',"); break;
	 default: sql.append("','U',"); break;
	}
	std::string signature;
	theApp->getTxnDB()->getDB()->escape(&(mSignature.front()), mSignature.size(), signature);
	sql.append(signature);
	sql.append(");");
	
	ScopedLock sl(theApp->getTxnDB()->getDBLock());
	Database* db=theApp->getTxnDB()->getDB();
	return db->executeSQL(sql.c_str());
}

Transaction::pointer Transaction::transactionFromSQL(const std::string& sql)
{
	std::string transID, fromID, toID, status;
	uint64 amount, fee;
	uint32 ledgerSeq, fromSeq, fromLedger, ident;
	std::vector<unsigned char> signature;
	signature.reserve(78);
	if(1)
	{
		ScopedLock sl(theApp->getTxnDB()->getDBLock());
		Database* db=theApp->getTxnDB()->getDB();

		if(!db->executeSQL(sql.c_str(), true) || !db->startIterRows() || !db->getNextRow())
			return Transaction::pointer();
		
		db->getStr("TransID", transID);
		db->getStr("FromID", fromID);
		fromSeq=db->getBigInt("FromSeq");
		fromLedger=db->getBigInt("FromLedger");
		db->getStr("ToID", toID);
		amount=db->getBigInt("Amount");
		fee=db->getBigInt("Fee");
		ledgerSeq=db->getBigInt("CommitSeq");
		ident=db->getBigInt("Identifier");
		db->getStr("Status", status);
		int sigSize=db->getBinary("Signature", &(signature.front()), signature.size());
		signature.resize(sigSize);
		db->endIterRows();
	}

	uint256 trID;
	uint160 frID, tID;
	trID.SetHex(transID);
	frID.SetHex(fromID);
	tID.SetHex(toID);	

	CKey::pointer pubkey=theApp->getPubKeyCache().locate(frID);
	if(!pubkey) return Transaction::pointer();
	
	TransStatus st(INVALID);
	switch(status[0])
	{
		case 'N': st=NEW; break;
		case 'A': st=INCLUDED; break;
		case 'C': st=CONFLICTED; break;
		case 'D': st=COMMITTED; break;
		case 'H': st=HELD; break;
	}
	
	return Transaction::pointer(new Transaction(trID, frID, tID, pubkey, amount, fee, fromSeq, fromLedger,
		ident, signature, ledgerSeq, st));

}

Transaction::pointer Transaction::load(const uint256& id)
{
	std::string sql="SELECT * FROM Transactions WHERE TransID='";
	sql.append(id.GetHex());
	sql.append("';");
	return transactionFromSQL(sql);
}

Transaction::pointer Transaction::findFrom(const uint160& fromID, uint32 seq)
{
	std::string sql="SELECT * FROM Transactions WHERE FromID='";
	sql.append(fromID.GetHex());
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
	Json::Value ret(Json::objectValue);
	ret["TransactionID"]=mTransactionID.GetHex();
	ret["Amount"]=boost::lexical_cast<std::string>(mAmount);
	ret["Fee"]=boost::lexical_cast<std::string>(mFee);
	if(mInLedger) ret["InLedger"]=mInLedger;

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

	Json::Value source(Json::objectValue);
	source["AccountID"]=NewcoinAddress(mAccountFrom).GetString();
	source["AccountSeq"]=mFromAccountSeq;
	source["Ledger"]=mSourceLedger;
	if(!!mIdent) source["Identifier"]=mIdent;

	Json::Value destination(Json::objectValue);
	destination["AccountID"]=NewcoinAddress(mAccountTo).GetString();

	if(decorate)
	{
		LocalAccount::pointer lac=theApp->getWallet().getLocalAccount(mAccountFrom);
		if(!!lac) source=lac->getJson();
		lac=theApp->getWallet().getLocalAccount(mAccountTo);
		if(!!lac) destination=lac->getJson();
	}
	if(paid) source["Paid"]=true;
	if(credited) destination["Credited"]=true;
	ret["Source"]=source;
	ret["Destination"]=destination;
	return ret;
}

