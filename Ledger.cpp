
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

Ledger::Ledger(const uint160& masterID, uint64 startAmount) :
	mFeeHeld(0), mTimeStamp(0), mLedgerSeq(0),
	mClosed(false), mValidHash(false), mAccepted(false)
{
	mTransactionMap=SHAMap::pointer(new SHAMap());
	mAccountStateMap=SHAMap::pointer(new SHAMap());
	
	AccountState::pointer startAccount=AccountState::pointer(new AccountState(masterID));
	startAccount->credit(startAmount);
	if(!addAccountState(startAccount))
		assert(false);
}

Ledger::Ledger(const uint256 &parentHash, const uint256 &transHash, const uint256 &accountHash,
	uint64 feeHeld, uint64 timeStamp, uint32 ledgerSeq)
		: mParentHash(parentHash), mTransHash(transHash), mAccountHash(accountHash),
		mFeeHeld(feeHeld), mTimeStamp(timeStamp), mLedgerSeq(ledgerSeq),
		mClosed(false), mValidHash(false), mAccepted(false)
{
	updateHash();
}

Ledger::Ledger(Ledger &prevLedger, uint64 ts) : mTimeStamp(ts), 
	mTransactionMap(new SHAMap()), mAccountStateMap(prevLedger.mAccountStateMap),
	mClosed(false), mValidHash(false), mAccepted(false)
{
	mParentHash=prevLedger.getHash();
	mLedgerSeq=prevLedger.mLedgerSeq+1;
}

void Ledger::updateHash()
{
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
	SHAMapItem::pointer item=mAccountStateMap->peekItem(uint160to256(accountID));
	if(!item)
	{
#ifdef DEBUG
		std::cerr << "   notfound" << std::endl;
#endif
		return AccountState::pointer();
	}
	return AccountState::pointer(new AccountState(item->getData()));
}

uint64 Ledger::getBalance(const uint160& accountID)
{
	ScopedLock l(mTransactionMap->Lock());
	SHAMapItem::pointer item=mAccountStateMap->peekItem(uint160to256(accountID));
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

Transaction::pointer Ledger::getTransaction(const uint256& transID)
{
	ScopedLock l(mTransactionMap->Lock());
	SHAMapItem::pointer item=mTransactionMap->peekItem(transID);
	if(!item) return Transaction::pointer();
	Transaction::pointer trans(new Transaction(item->getData(), true));
	if(trans->getStatus()==NEW) trans->setStatus(mClosed ? COMMITTED : INCLUDED, mLedgerSeq);
	return trans;
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
	}
	catch (SHAMapException)
	{
		return TR_ERROR;
	}
}

Ledger::TransResult Ledger::hasTransaction(Transaction::pointer trans)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	if(mTransactionMap==NULL) return TR_ERROR;
	try
	{
		Transaction::pointer t=getTransaction(trans->getID());
		if(t==NULL) return TR_NOTFOUND;
		return TR_SUCCESS;
	}
	catch (SHAMapException)
	{
		return TR_ERROR;
	}
}

Ledger::pointer Ledger::closeLedger(uint64 timeStamp)
{ // close this ledger, return a pointer to the next ledger
  // CAUTION: New ledger needs its SHAMap's connected to storage
	setClosed();
	return Ledger::pointer(new Ledger(*this, timeStamp));
}

bool Ledger::unitTest()
{
    LocalAccount l1(true), l2(true);
    assert(l1.peekPubKey());

    uint160 la1(l1.getAddress()), la2(l2.getAddress());
#ifdef DEBUG
    std::cerr << "Account1: " << la1.GetHex() << std::endl;
    std::cerr << "Account2: " << la2.GetHex() << std::endl;
#endif

    Ledger::pointer ledger(new Ledger(la1, 100000));
    l1.mAmount=100000;
    
    ledger=Ledger::pointer(new Ledger(*ledger, 0));

    AccountState::pointer as=ledger->getAccountState(la1);
    assert(as);
    assert(as->getBalance()==100000);
    assert(as->getSeq()==0);
    as=ledger->getAccountState(la2);
    assert(!as); 

	Transaction::pointer t(new Transaction(NEW, l1, l1.mSeqNum, l2.getAddress(), 2500, 0, 1));
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

	ScopedLock sl(theApp->getDBLock());
	theApp->getDB()->executeSQL(sql.c_str());

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
		ScopedLock sl(theApp->getDBLock());
		Database *db=theApp->getDB();
		if(!db->executeSQL(sql.c_str()) || !db->getNextRow())
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

#if 0

Ledger::pointer Ledger::getParent()
{
	if(!mParent)
	{
		mParent=theApp->getLedgerMaster().getLedger(mParentHash);
	}
	return(mParent);
}

// TODO: we can optimize so the ledgers only hold the delta from the accepted ledger
// TODO: check to make sure the ledger is consistent after we load it
bool Ledger::load(const uint256& hash)
{
	Database* db=theApp->getDB();

	string sql="SELECT * from Ledgers where hash=";
	string hashStr;
	db->escape(hash.begin(),hash.GetSerializeSize(),hashStr);
	sql.append(hashStr);

	if(db->executeSQL(sql.c_str()))
	{
		if(db->getNextRow())
		{
			mIndex=db->getInt("LedgerIndex");
			mHash=hash;
			mValidSig=false;
			mAccounts.clear();
			mTransactions.clear();
			mDiscardedTransactions.clear();

			db->getBinary("ParentHash",mParentHash.begin(),mParentHash.GetSerializeSize());
			mFeeHeld=db->getBigInt("FeeHeld");


			char buf[100];
			sql="SELECT Transactions.* from Transactions,LedgerTransactionMap where Transactions.TransactionID=LedgerTransactionMap.TransactionID and LedgerTransactionMap.LedgerID=";
			sprintf(buf, "%d", db->getInt(0));
			sql.append(buf);
			if(db->executeSQL(sql.c_str()))
			{
				unsigned char tbuf[1000];
				while(db->getNextRow())
				{
					Transaction::pointer trans=Transaction::pointer(new newcoin::Transaction());
					trans->set_amount( db->getBigInt("Amount"));
					trans->set_seqnum( db->getInt("seqnum"));
					trans->set_ledgerindex( db->getInt("ledgerIndex"));
					db->getBinary("from",tbuf,1000);
					trans->set_from(tbuf,20);
					db->getBinary("dest",tbuf,1000);
					trans->set_dest(tbuf,20);
					db->getBinary("pubkey",tbuf,1000);
					trans->set_pubkey(tbuf,128);
					db->getBinary("sig",tbuf,1000);
					trans->set_sig(tbuf,32);

					mTransactions.push_back(trans);
				}
			}

			sql="SELECT Accounts.* from Acconts,LedgerAcountMap where Accounts.AccountID=LedgerAccountMap.AccountID and LedgerAccountMap.LedgerID=";
			sql.append(buf);
			if(db->executeSQL(sql.c_str()))
			{
				while(db->getNextRow())
				{
					uint160 address;
					db->getBinary("Address",address.begin(),address.GetSerializeSize());

					mAccounts[address].first=db->getBigInt("Amount");
					mAccounts[address].second=db->getInt("SeqNum");

				}
			}
			return(true);
		}
	}
	return(false);
}

uint256& Ledger::getSignature()
{
	if(!mValidSig) sign();
	return(mSignature); 
}

void Ledger::publishValidation()
{
	PackedMessage::pointer packet=Peer::createValidation(shared_from_this());
	theApp->getConnectionPool().relayMessage(NULL,packet);
}

/*
uint64 Ledger::getAmount(std::string address)
{
	return(mAccounts[NewcoinAddress:: address].first);
}*/

// returns true if the from account has enough for the transaction and seq num is correct
bool Ledger::addTransaction(Transaction::pointer trans,bool checkDuplicate)
{
	if(checkDuplicate && hasTransaction(trans)) return(false);

	if(mParent)
	{ // check the lineage of the from addresses
		uint160 address=protobufTo160(trans->from());
		if(mAccounts.count(address))
		{
			pair<uint64,uint32> account=mAccounts[address];
			if( (account.first<trans->amount()) &&
				(trans->seqnum()==account.second) )
			{
				account.first -= trans->amount();
				account.second++;
				mAccounts[address]=account;


				uint160 destAddress=protobufTo160(trans->dest());

				Account destAccount=mAccounts[destAddress];
				destAccount.first += trans->amount();
				mAccounts[destAddress]=destAccount;


				mValidSig=false;
				mValidHash=false;
				mTransactions.push_back(trans);
				if(mChild)
				{
					mChild->parentAddedTransaction(trans);
				}
				return(true);
			}else
			{
				mDiscardedTransactions.push_back(trans);
				return false;
			}
		}else 
		{
			mDiscardedTransactions.push_back(trans);
			return false;
		}
		
	}else
	{ // we have no way to know so just hold on to it but don't add to the accounts
		mValidSig=false;
		mValidHash=false;
		mDiscardedTransactions.push_back(trans);
		return(true);
	}
}

// Don't check the amounts. We will do this at the end.
void Ledger::addTransactionAllowNeg(Transaction::pointer trans)
{
	uint160 fromAddress=protobufTo160(trans->from());

	if(mAccounts.count(fromAddress))
	{
		Account fromAccount=mAccounts[fromAddress];
		if(trans->seqnum()==fromAccount.second) 
		{
			fromAccount.first -= trans->amount();
			fromAccount.second++;
			mAccounts[fromAddress]=fromAccount;
			
			uint160 destAddress=protobufTo160(trans->dest());

			Account destAccount=mAccounts[destAddress];
			destAccount.first += trans->amount();
			mAccounts[destAddress]=destAccount;

			mTransactions.push_back(trans);
	
		}else
		{  // invalid seqnum
			mDiscardedTransactions.push_back(trans);
		}
	}else
	{
		if(trans->seqnum()==0)
		{
			
			mAccounts[fromAddress]=Account(-((int64)trans->amount()),1);

			uint160 destAddress=protobufTo160(trans->dest());

			Account destAccount=mAccounts[destAddress];
			destAccount.first += trans->amount();
			mAccounts[destAddress]=destAccount;

			mTransactions.push_back(trans);

		}else
		{
			mDiscardedTransactions.push_back(trans);
		}
	}
}



// start from your parent and go through every transaction
// calls this on its child if recursive is set
void Ledger::recalculate(bool recursive)
{
	if(mParent)
	{
		mValidSig=false;
		mValidHash=false;

		mAccounts.clear();
		mAccounts=mParent->getAccounts();
		list<Transaction::pointer> firstTransactions=mTransactions;
		list<Transaction::pointer> secondTransactions=mDiscardedTransactions;

		mTransactions.clear();
		mDiscardedTransactions.clear();

		firstTransactions.sort(gTransactionSorter);
		secondTransactions.sort(gTransactionSorter);

		// don't check balances until the end
		BOOST_FOREACH(Transaction::pointer trans,firstTransactions)
		{
			addTransactionAllowNeg(trans);
		}

		BOOST_FOREACH(Transaction::pointer trans,secondTransactions)
		{
			addTransactionAllowNeg(trans);
		}
		correctAccounts();
		


		if(mChild && recursive) mChild->recalculate();
	}else
	{
		cout << "Can't recalculate if there is no parent" << endl;
	}
}



void Ledger::parentAddedTransaction(Transaction::pointer cause)
{
	// TODO: optimize we can make this more efficient at some point. For now just redo everything

	recalculate();

	/* 
	// IMPORTANT: these changes can't change the sequence number. This means we only need to check the dest account
	// If there was a seqnum change we have to re-do all the transactions again

	// There was a change to the balances of the parent ledger
	// This could cause:
	//		an account to now be negative so we have to discard one
	//		a discarded transaction to be pulled back in
	//		seqnum invalidation
	uint160 fromAddress=protobufTo160(cause->from());
	uint160 destAddress=protobufTo160(cause->dest());
	
	Account* fromAccount=getAccount(fromAddress);
	Account* destAccount=getAccount(destAddress);

	if(fromAccount)
	{
		if(fromAccount->first<cause->amount())
		{
			fromAccount->first -= cause->amount();
			fromAccount->second = cause->seqnum()+1;
			mAccounts[fromAddress] = *fromAccount;
		}else cout << "This shouldn't happen2" << endl;
	}else
	{
		cout << "This shouldn't happen" << endl;
	}

	if(destAccount)
	{
		destAccount->first += cause->amount();
		mAccounts[destAddress]= *destAccount;
	}else
	{
		mAccounts[destAddress]=pair<uint64,uint32>(cause->amount(),cause->seqnum());
	}
	
	// look for discarded transactions
	BOOST_FOREACH(Transaction::pointer trans,)
	*/
}

bool Ledger::hasTransaction(Transaction::pointer needle)
{
	BOOST_FOREACH(Transaction::pointer trans,mTransactions)
	{
		if( Transaction::isEqual(needle,trans) ) return(true);
	}

	BOOST_FOREACH(Transaction::pointer disTrans,mDiscardedTransactions)
	{
		if( Transaction::isEqual(needle,disTrans) ) return(true);
	}

	return(false);
}

// Ledgers are compatible if both sets of transactions merged together would lead to the same ending balance
bool Ledger::isCompatible(Ledger::pointer other)
{
	Ledger::pointer l1=Ledger::pointer(new Ledger(*this));
	Ledger::pointer l2=Ledger::pointer(new Ledger(*other));

	l1->mergeIn(l2);
	l2->mergeIn(l1);

	map<uint160, Account > a1=l1->getAccounts();
	map<uint160, Account > a2=l2->getAccounts();

	return(a1==a2);

}

void Ledger::mergeIn(Ledger::pointer other)
{
	list<Transaction::pointer>& otherTransactions=other->getTransactions();
	BOOST_FOREACH(Transaction::pointer trans,otherTransactions)
	{
		addTransactionAllowNeg(trans);
	}

	correctAccounts();
}

void Ledger::correctAccounts()
{
	BOOST_FOREACH(PAIR(const uint160, Account)& fullAccount, mAccounts)
	{
		if(fullAccount.second.first <0 )
		{
			correctAccount(fullAccount.first);
		}
	}
}

// Must look for transactions to discard to make this account positive
// When we chuck transactions it might cause other accounts to need correcting
void Ledger::correctAccount(const uint160& address)
{
	list<uint160> effected;

	// do this in reverse so we take of the higher seqnum first
	for( list<Transaction::pointer>::reverse_iterator iter=mTransactions.rbegin(); iter != mTransactions.rend(); )
	{
		Transaction::pointer trans= *iter;
		if(protobufTo160(trans->from()) == address)
		{
			Account fromAccount=mAccounts[address];
			assert(fromAccount.second==trans->seqnum()+1);
			if(fromAccount.first<0)
			{
				fromAccount.first += trans->amount();
				fromAccount.second --;

				mAccounts[address]=fromAccount;

				uint160 destAddress=protobufTo160(trans->dest());
				Account destAccount=mAccounts[destAddress];
				destAccount.first -= trans->amount();
				mAccounts[destAddress]=destAccount;
				if(destAccount.first<0) effected.push_back(destAddress);

				list<Transaction::pointer>::iterator temp=mTransactions.erase( --iter.base() );
				if(fromAccount.first>=0) break; 

				iter=list<Transaction::pointer>::reverse_iterator(temp);
			}else break;	
		}else iter--;
	}

	BOOST_FOREACH(uint160& address,effected)
	{
		correctAccount(address);
	}

}

#endif
