#include "boost/lexical_cast.hpp"

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

Transaction::Transaction(TransStatus status, LocalAccount::pointer fromLocalAccount, uint32 fromSeq,
		const uint160& toAccount, uint64 amount, uint32 ident, uint32 ledger) :
			mAccountTo(toAccount), mAmount(amount), mFromAccountSeq(fromSeq), mSourceLedger(ledger),
			mIdent(ident), mInLedger(0), mStatus(NEW)
{
	mAccountFrom=fromLocalAccount->getAddress();
	mFromPubKey=fromLocalAccount->getPublicKey();
	assert(mFromPubKey);
	updateFee();
	sign(fromLocalAccount);
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
	mFromPubKey=CKey::pointer(new CKey());
	if(!mFromPubKey->SetPubKey(pubKey)) return;
	mAccountFrom=Hash160(pubKey);
	mFromPubKey=theApp->getPubKeyCache().store(mAccountFrom, mFromPubKey);

	updateID();

	if(!validate || checkSign())
		mStatus=NEW;
}

bool Transaction::sign(LocalAccount::pointer fromLocalAccount)
{
	CKey::pointer privateKey=fromLocalAccount->getPrivateKey();
	if(!privateKey) return false;

	if( (mAmount==0) || (mSourceLedger==0) || (mAccountTo==0) )
	{
		assert(false);
		return false;
	}
	if(mAccountFrom!=fromLocalAccount->getAddress())
	{
		assert(false);
		return false;
	}
	Serializer::pointer signBuf=getRaw(true);
	assert(signBuf->getLength()==73+4);
	if(!signBuf->makeSignature(mSignature, *privateKey))
	{
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
	return mFromPubKey->Verify(getRaw(true)->getSHA512Half(), mSignature);
}

Serializer::pointer Transaction::getRaw(bool prefix) const
{
	Serializer::pointer ret(new Serializer(77));
	if(prefix) ret->add32(0x54584e00u);
	ret->add160(mAccountTo);
	ret->addRaw(mFromPubKey->GetPubKey());
	ret->add64(mAmount);
	ret->add32(mFromAccountSeq);
	ret->add32(mInLedger);
	ret->add32(mIdent);
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

		if(!db->executeSQL(sql.c_str()) || !db->startIterRows() || !db->getNextRow())
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
	std::string sql="SELECT * FROM Transactions WHERE Hash='";
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
	bool checkFirstTransactions, bool checkSecondTransactions,
	const std::map<uint256, std::pair<SHAMapItem::pointer, SHAMapItem::pointer> >& inMap,
	std::map<uint256, std::pair<Transaction::pointer, Transaction::pointer> >& outMap)
{ // convert a straight SHAMap payload difference to a transaction difference table
  // return value: true=ledgers are valid, false=a ledger is invalid
  	bool ret=true;
	std::map<uint256, std::pair<SHAMapItem::pointer, SHAMapItem::pointer> >::const_iterator it;
	for(it=inMap.begin(); it!=inMap.end(); ++it)
	{
		const uint256& id=it->first;
		const SHAMapItem::pointer& first=it->second.first;
		const SHAMapItem::pointer& second=it->second.second;
	
		Transaction::pointer firstTrans, secondTrans;	
		if(!!first)
		{ // transaction in our table
			firstTrans=Transaction::pointer(new Transaction(first->getData(), checkFirstTransactions));
			if( (firstTrans->getStatus()==INVALID) || (firstTrans->getID()!=id) )
			{
				firstTrans->setStatus(INVALID, firstLedgerSeq);
				ret=false;
			}
			else firstTrans->setStatus(INCLUDED, firstLedgerSeq);
		}

		if(!!second)
		{ // transaction in other table
			secondTrans=Transaction::pointer(new Transaction(second->getData(), checkSecondTransactions));
			if( (secondTrans->getStatus()==INVALID) || (secondTrans->getID()!=id) )
			{
				secondTrans->setStatus(INVALID, secondLedgerSeq);
				ret=false;
			}
			else secondTrans->setStatus(INCLUDED, secondLedgerSeq);
		}
		assert(firstTrans || secondTrans);
		if(firstTrans && secondTrans && (firstTrans->getStatus()!=INVALID) && (secondTrans->getStatus()!=INVALID))
			ret=false;	// one or the other SHAMap is structurally invalid or a miracle has happened

		outMap[id]=std::pair<Transaction::pointer, Transaction::pointer>(firstTrans, secondTrans);
	}
	return ret;
}
