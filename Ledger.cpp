
#include <iostream>
#include <fstream>

#include <boost/lexical_cast.hpp>

#include "Application.h"
#include "Ledger.h"
#include "newcoin.pb.h"
#include "PackedMessage.h"
#include "Config.h"
#include "Conversion.h"
#include "BitcoinUtil.h"
#include "Wallet.h"
#include "BinaryFormats.h"

Ledger::Ledger(const uint160& masterID, uint64 startAmount) :
	mFeeHeld(0), mTimeStamp(0), mLedgerSeq(0),
	mClosed(false), mValidHash(false), mAccepted(false), mImmutable(false)
{
	mTransactionMap=SHAMap::pointer(new SHAMap());
	mAccountStateMap=SHAMap::pointer(new SHAMap(0));
	
	AccountState::pointer startAccount=AccountState::pointer(new AccountState(masterID));
	startAccount->credit(startAmount);
	if(!addAccountState(startAccount))
		assert(false);
}

Ledger::Ledger(const uint256 &parentHash, const uint256 &transHash, const uint256 &accountHash,
	uint64 feeHeld, uint64 timeStamp, uint32 ledgerSeq)
		: mParentHash(parentHash), mTransHash(transHash), mAccountHash(accountHash),
		mFeeHeld(feeHeld), mTimeStamp(timeStamp), mLedgerSeq(ledgerSeq),
		mClosed(false), mValidHash(false), mAccepted(false), mImmutable(false)
{
	updateHash();
}

Ledger::Ledger(Ledger &prevLedger, uint64 ts) : mTimeStamp(ts), 
	mClosed(false), mValidHash(false), mAccepted(false), mImmutable(false),
	mTransactionMap(new SHAMap()), mAccountStateMap(prevLedger.mAccountStateMap)
{
	mParentHash=prevLedger.getHash();
	mLedgerSeq=prevLedger.mLedgerSeq+1;
	mAccountStateMap->setSeq(mLedgerSeq);
}

Ledger::Ledger(const std::vector<unsigned char>& rawLedger) : mFeeHeld(0), mTimeStamp(0),
	mLedgerSeq(0), mClosed(false), mValidHash(false), mAccepted(false), mImmutable(true)
{
	Serializer s(rawLedger);
	// 32seq, 64fee, 256phash, 256thash, 256ahash, 64ts
	if(!s.get32(mLedgerSeq, BLgPIndex)) return;
	if(!s.get64(mFeeHeld, BLgPFeeHeld)) return;
	if(!s.get256(mParentHash, BLgPPrevLg)) return;
	if(!s.get256(mTransHash, BLgPTxT)) return;
	if(!s.get256(mAccountHash, BLgPAcT)) return;
	if(!s.get64(mTimeStamp, BLgPClTs)) return;
	updateHash();
	if(mValidHash)
	{
		mTransactionMap=SHAMap::pointer(new SHAMap());
		mAccountStateMap=SHAMap::pointer(new SHAMap(mLedgerSeq));
	}
}

void Ledger::updateHash()
{
	if(!mImmutable)
	{
		if(mTransactionMap) mTransHash=mTransactionMap->getHash();
		else mTransHash=0;
		if(mAccountStateMap) mAccountHash=mAccountStateMap->getHash();
		else mAccountHash=0;
	}

	Serializer s(116);
	addRaw(s);
	mHash=s.getSHA512Half();
	mValidHash=true;
}

void Ledger::addRaw(Serializer &s)
{
	s.add32(mLedgerSeq);
	s.add64(mFeeHeld);
	s.add256(mParentHash);
	s.add256(mTransHash);
	s.add256(mAccountHash);
	s.add64(mTimeStamp);
}

AccountState::pointer Ledger::getAccountState(const uint160& accountID)
{
#ifdef DEBUG
	std::cerr << "Ledger:getAccountState(" << accountID.GetHex() << ")" << std::endl;
#endif
	ScopedLock l(mTransactionMap->Lock());
	SHAMapItem::pointer item=mAccountStateMap->peekItem(accountID.to256());
	if(!item)
	{
#ifdef DEBUG
		std::cerr << "   notfound" << std::endl;
#endif
		return AccountState::pointer();
	}
	return AccountState::pointer(new AccountState(item->getData()));
}

uint64 Ledger::getBalance(const uint160& accountID) const
{
	ScopedLock l(mTransactionMap->Lock());
	SHAMapItem::pointer item=mAccountStateMap->peekItem(accountID.to256());
	if(!item) return 0;
	return AccountState(item->getData()).getBalance();
}

bool Ledger::updateAccountState(AccountState::pointer state)
{
	assert(!mAccepted);
	SHAMapItem::pointer item(new SHAMapItem(state->getAccountID(), state->getRaw()));
	return mAccountStateMap->updateGiveItem(item);
}

bool Ledger::addAccountState(AccountState::pointer state)
{
	assert(!mAccepted);
	assert( (state->getBalance()==0) || (state->getSeq()>0) );
	SHAMapItem::pointer item(new SHAMapItem(state->getAccountID(), state->getRaw()));
	return mAccountStateMap->addGiveItem(item);
}

bool Ledger::addTransaction(Transaction::pointer trans)
{ // low-level - just add to table
	assert(!mAccepted);
	assert(!!trans->getID());
	SHAMapItem::pointer item(new SHAMapItem(trans->getID(), trans->getSigned()->getData()));
	return mTransactionMap->addGiveItem(item);
}

bool Ledger::delTransaction(const uint256& transID)
{
	assert(!mAccepted);
	return mTransactionMap->delItem(transID); 
}

bool Ledger::hasTransaction(const uint256& transID) const
{
	return mTransactionMap->hasItem(transID);
}

Transaction::pointer Ledger::getTransaction(const uint256& transID) const
{
	SHAMapItem::pointer item=mTransactionMap->peekItem(transID);
	if(!item) return Transaction::pointer();

	Transaction::pointer txn=theApp->getMasterTransaction().fetch(transID, false);
	if(txn) return txn;

	txn=Transaction::pointer(new Transaction(item->getData(), true));
	if(txn->getStatus()==NEW) txn->setStatus(mClosed ? COMMITTED : INCLUDED, mLedgerSeq);

	theApp->getMasterTransaction().canonicalize(txn, false);
	return txn;
}

Ledger::TransResult Ledger::applyTransaction(Transaction::pointer trans)
{
	assert(!mAccepted);
	boost::recursive_mutex::scoped_lock sl(mLock);
	if(trans->getSourceLedger()>mLedgerSeq) return TR_BADLSEQ;

	if(trans->getAmount()<trans->getFee())
	{
#ifdef DEBUG
			std::cerr << "Transaction for " << trans->getAmount() << ", but fee is " <<
					trans->getFee() << std::endl;
#endif
		return TR_TOOSMALL;
	}

	if(!mTransactionMap || !mAccountStateMap) return TR_ERROR;
	try
	{
		// already applied?
		Transaction::pointer dupTrans=getTransaction(trans->getID());
		if(dupTrans) return TR_ALREADY;

		// accounts exist?
		AccountState::pointer fromAccount=getAccountState(trans->getFromAccount());
		AccountState::pointer toAccount=getAccountState(trans->getToAccount());

		// temporary code -- if toAccount doesn't exist but fromAccount does, create it
		if(!!fromAccount && !toAccount)
		{
			toAccount=AccountState::pointer(new AccountState(trans->getToAccount()));
			toAccount->incSeq(); // an account in a ledger has a sequence of 1
			updateAccountState(toAccount);
		}

		if(!fromAccount || !toAccount) return TR_BADACCT;

		// pass sanity checks?
		if(fromAccount->getBalance()<trans->getAmount())
		{
#ifdef DEBUG
			std::cerr << "Transaction for " << trans->getAmount() << ", but account has " <<
					fromAccount->getBalance() << std::endl;
#endif
			return TR_INSUFF;
		}
#ifdef DEBUG
		if(fromAccount->getSeq()!=trans->getFromAccountSeq())
			std::cerr << "aSeq=" << fromAccount->getSeq() << ", tSeq=" << trans->getFromAccountSeq() << std::endl;
#endif
		if(fromAccount->getSeq()>trans->getFromAccountSeq()) return TR_PASTASEQ;
		if(fromAccount->getSeq()<trans->getFromAccountSeq()) return TR_PREASEQ;

		// apply
		fromAccount->charge(trans->getAmount());
		fromAccount->incSeq();
		toAccount->credit(trans->getAmount()-trans->getFee());
		mFeeHeld+=trans->getFee();
		trans->setStatus(INCLUDED, mLedgerSeq);

		updateAccountState(fromAccount);
		updateAccountState(toAccount);
		addTransaction(trans);

		return TR_SUCCESS;
	}
	catch (SHAMapException)
	{
		return TR_ERROR;
	}
}

Ledger::TransResult Ledger::removeTransaction(Transaction::pointer trans)
{ // high-level - reverse application of transaction
	assert(!mAccepted);
	boost::recursive_mutex::scoped_lock sl(mLock);
	if(!mTransactionMap || !mAccountStateMap) return TR_ERROR;
	try
	{
		Transaction::pointer ourTrans=getTransaction(trans->getID());
		if(!ourTrans) return TR_NOTFOUND;

		// accounts exist
		AccountState::pointer fromAccount=getAccountState(trans->getFromAccount());
		AccountState::pointer toAccount=getAccountState(trans->getToAccount());
		if(!fromAccount || !toAccount) return TR_BADACCT;

		// pass sanity checks?
		if(toAccount->getBalance()<trans->getAmount()) return TR_INSUFF;
		if(fromAccount->getSeq()!=(trans->getFromAccountSeq()+1)) return TR_PASTASEQ;
		
		// reverse
		fromAccount->credit(trans->getAmount());
		fromAccount->decSeq();
		toAccount->charge(trans->getAmount()-trans->getFee());
		mFeeHeld-=trans->getFee();
		trans->setStatus(REMOVED, mLedgerSeq);
		
		if(!delTransaction(trans->getID()))
		{
			assert(false);
			return TR_ERROR;
		}
		updateAccountState(fromAccount);
		updateAccountState(toAccount);
		return TR_SUCCESS;
	}
	catch (SHAMapException)
	{
		return TR_ERROR;
	}
}

Ledger::TransResult Ledger::hasTransaction(Transaction::pointer trans)
{ // Is this transaction in this ledger? If not, could it go in it?
	boost::recursive_mutex::scoped_lock sl(mLock);
	if(mTransactionMap==NULL) return TR_ERROR;
	try
	{
		Transaction::pointer t=getTransaction(trans->getID());
		if(!!t) return TR_ALREADY;
		
		if(trans->getSourceLedger()>mLedgerSeq) return TR_BADLSEQ;

		AccountState::pointer fromAccount=getAccountState(trans->getFromAccount());
		if(!fromAccount) return TR_BADACCT; // cannot send from non-existent account

		// may be in a previous ledger
		if(fromAccount->getSeq()>trans->getFromAccountSeq()) return TR_PASTASEQ;

		if(fromAccount->getSeq()<trans->getFromAccountSeq()) return TR_PREASEQ;
		if(fromAccount->getBalance()<trans->getAmount()) return TR_INSUFF;
		return TR_NOTFOUND;
	}
	catch (SHAMapException)
	{
		return TR_ERROR;
	}
}

Ledger::pointer Ledger::closeLedger(uint64 timeStamp)
{ // close this ledger, return a pointer to the next ledger
	// CAUTION: New ledger needs its SHAMap's connected to storage
	updateHash();
	setClosed();
	return Ledger::pointer(new Ledger(*this, timeStamp));
}

void LocalAccount::syncLedger()
{
	AccountState::pointer as=theApp->getMasterLedger().getAccountState(getAddress());
	if(!as)	mLgrBalance=0;
	else
	{
		mLgrBalance=as->getBalance();
		if( (mLgrBalance!=0) && (mTxnSeq==0) ) mTxnSeq=1;
		if(mTxnSeq<as->getSeq()) mTxnSeq=as->getSeq();
	}
}

bool Ledger::unitTest()
{
	uint160 la1=theApp->getWallet().addFamily(CKey::PassPhraseToKey("This is my payphrase!"), false);
	uint160 la2=theApp->getWallet().addFamily(CKey::PassPhraseToKey("Another payphrase"), false);

	LocalAccount::pointer l1=theApp->getWallet().getLocalAccount(la1, 0);
	LocalAccount::pointer l2=theApp->getWallet().getLocalAccount(la2, 0);

	assert(l1->getAddress()==la1);

#ifdef DEBUG
	std::cerr << "Account1: " << la1.GetHex() << std::endl;
	std::cerr << "Account2: " << la2.GetHex() << std::endl;
#endif

	Ledger::pointer ledger(new Ledger(la1, 100000));
	
	ledger=Ledger::pointer(new Ledger(*ledger, 0));

	AccountState::pointer as=ledger->getAccountState(la1);
	assert(as);
	assert(as->getBalance()==100000);
	assert(as->getSeq()==0);
	as=ledger->getAccountState(la2);
	assert(!as); 

	Transaction::pointer t(new Transaction(l1, l2->getAddress(), 2500, 0, 1));
	assert(!!t->getID());

	Ledger::TransResult tr=ledger->applyTransaction(t);
#ifdef DEBUG
	std::cerr << "Transaction: " << tr << std::endl;
#endif
	assert(tr==TR_SUCCESS);

	return true;
}

uint256 Ledger::getHash()
{
	if(!mValidHash) updateHash();
	return(mHash); 
}

void Ledger::saveAcceptedLedger(Ledger::pointer ledger)
{
	std::string sql="INSERT INTO Ledgers "
		"(LedgerHash,LedgerSeq,PrevHash,FeeHeld,ClosingTime,AccountSetHash,TransSetHash) VALUES ('";
	sql.append(ledger->getHash().GetHex());
	sql.append("','");
	sql.append(boost::lexical_cast<std::string>(ledger->mLedgerSeq));
	sql.append("','");
	sql.append(ledger->mParentHash.GetHex());
	sql.append("','");
	sql.append(boost::lexical_cast<std::string>(ledger->mFeeHeld));
	sql.append("','");
	sql.append(boost::lexical_cast<std::string>(ledger->mTimeStamp));
	sql.append("','");
	sql.append(ledger->mAccountHash.GetHex());
	sql.append("','");
	sql.append(ledger->mTransHash.GetHex());
	sql.append("');");

	ScopedLock sl(theApp->getLedgerDB()->getDBLock());
	theApp->getLedgerDB()->getDB()->executeSQL(sql.c_str());

	// write out dirty nodes
	while(ledger->mTransactionMap->flushDirty(64, TRANSACTION_NODE, ledger->mLedgerSeq))
	{ ; }
	while(ledger->mAccountStateMap->flushDirty(64, ACCOUNT_NODE, ledger->mLedgerSeq))
	{ ; }

}

Ledger::pointer Ledger::getSQL(const std::string& sql)
{
	uint256 ledgerHash, prevHash, accountHash, transHash;
	uint64 feeHeld, closingTime;
	uint32 ledgerSeq;
	std::string hash;

	if(1)
	{
		ScopedLock sl(theApp->getLedgerDB()->getDBLock());
		Database *db=theApp->getLedgerDB()->getDB();
		if(!db->executeSQL(sql.c_str()) || !db->startIterRows() || !db->getNextRow())
			 return Ledger::pointer();

		db->getStr("LedgerHash", hash);
		ledgerHash.SetHex(hash);
		db->getStr("PrevHash", hash);
		prevHash.SetHex(hash);
		db->getStr("AccountSetHash", hash);
		accountHash.SetHex(hash);
		db->getStr("TransSetHash", hash);
		transHash.SetHex(hash);
		feeHeld=db->getBigInt("FeeHeld");
		closingTime=db->getBigInt("ClosingTime");
		ledgerSeq=db->getBigInt("LedgerSeq");
		db->endIterRows();
	}
	
	Ledger::pointer ret(new Ledger(prevHash, transHash, accountHash, feeHeld, closingTime, ledgerSeq));
	if(ret->getHash()!=ledgerHash)
	{
		assert(false);
		return Ledger::pointer();
	}
	return ret;
}

Ledger::pointer Ledger::loadByIndex(uint32 ledgerIndex)
{
	std::string sql="SELECT * from Ledgers WHERE LedgerSeq='";
	sql.append(boost::lexical_cast<std::string>(ledgerIndex));
	sql.append("';");
	return getSQL(sql);
}

Ledger::pointer Ledger::loadByHash(const uint256& ledgerHash)
{
	std::string sql="SELECT * from Ledgers WHERE LedgerHash='";
	sql.append(ledgerHash.GetHex());
	sql.append("';");
	return getSQL(sql);
}

void Ledger::addJson(Json::Value& ret)
{
	Json::Value ledger(Json::objectValue);

	boost::recursive_mutex::scoped_lock sl(mLock);
	ledger["ParentHash"]=mParentHash.GetHex();

	if(mClosed)
	{
		ledger["Hash"]=mHash.GetHex();
		ledger["TransactionHash"]=mTransHash.GetHex();
		ledger["AccountHash"]=mAccountHash.GetHex();
		ledger["Closed"]=true;
		ledger["Accepted"]=mAccepted;
	}
	else ledger["Closed"]=false;
	ret[boost::lexical_cast<std::string>(mLedgerSeq)]=ledger;
}

Ledger::pointer Ledger::switchPreviousLedger(Ledger::pointer oldPrevious, Ledger::pointer newPrevious, int limit)
{
	// Build a new ledger that can replace this ledger as the active ledger,
	// with a different previous ledger. We assume our ledger is trusted, as is its
	// previous ledger. We make no assumptions about the new previous ledger.

	int count;

	// 1) Validate sequences and make sure the specified ledger is a valid prior ledger
	if(newPrevious->getLedgerSeq()!=oldPrevious->getLedgerSeq()) return Ledger::pointer();

	// 2) Begin building a new ledger with the specified ledger as previous.
	Ledger* newLedger=new Ledger(*newPrevious, mTimeStamp);

	// 3) For any transactions in our previous ledger but not in the new previous ledger, add them to the set
	SHAMap::SHAMapDiff mapDifferences;
	std::map<uint256, std::pair<Transaction::pointer, Transaction::pointer> > TxnDiff;
	if(!newPrevious->mTransactionMap->compare(oldPrevious->mTransactionMap, mapDifferences, limit))
		return Ledger::pointer();
	if(!Transaction::convertToTransactions(oldPrevious->getLedgerSeq(), newPrevious->getLedgerSeq(),
			false, true, mapDifferences, TxnDiff))
		return Ledger::pointer(); // new previous ledger contains invalid transactions

	// 4) Try to add those transactions to the new ledger.
	do
	{
		count=0;
		std::map<uint256, std::pair<Transaction::pointer, Transaction::pointer> >::iterator it=TxnDiff.begin();
		while(it!=TxnDiff.end())
		{
			Transaction::pointer& tx=it->second.second;
			if(!tx || newLedger->addTransaction(tx))
			{
				count++;
				TxnDiff.erase(it++);
			}
			else ++it;
		}
	} while(count!=0);

	// WRITEME: Handle rejected transactions left in TxnDiff

	// 5) Try to add transactions from this ledger to the new ledger.
	std::map<uint256, Transaction::pointer> txnMap;
	for(SHAMapItem::pointer mit=peekTransactionMap()->peekFirstItem();
			!!mit;
			mit=peekTransactionMap()->peekNextItem(mit->getTag()))
	{
		uint256 txnID=mit->getTag();
		Transaction::pointer tx=theApp->getMasterTransaction().fetch(txnID, false);
		if(!tx) tx=Transaction::pointer(new Transaction(mit->peekData(), false));
		txnMap.insert(std::make_pair(txnID, tx));
	}

	do
	{
		count=0;
		std::map<uint256, Transaction::pointer>::iterator it=txnMap.begin();
		while(it!=txnMap.end())
		{
			if(newLedger->addTransaction(it->second))
			{
				count++;
				txnMap.erase(it++);
			}
			else ++it;
		}
	} while(count!=0);


	// WRITEME: Handle rejected transactions left in txnMap

	return Ledger::pointer(newLedger);
}
