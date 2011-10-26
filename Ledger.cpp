#include "Ledger.h"
#include "newcoin.pb.h"
#include "PackedMessage.h"
#include "Application.h"
#include "Config.h"
#include "BitcoinUtil.h"
#include <boost/foreach.hpp>
#include <iostream>
#include <fstream>

using namespace boost;
using namespace std;

Ledger::Ledger()
{
	mIndex=0;
	mValidSig=false;
	mValidHash=false;
	mValidationSeqNum=0;
}

Ledger::Ledger(uint32 index)
{
	mIndex=index;
	mValidSig=false;
	mValidHash=false;
	mValidationSeqNum=0;
}

Ledger::Ledger(Ledger::pointer other)
{
	mValidSig=false;
	mValidHash=false;
	mValidationSeqNum=0;
	mIndex=other->getIndex();
	mergeIn(other);
}

Ledger::Ledger(newcoin::FullLedger& ledger)
{
	setTo(ledger);
}

// TODO: we should probably make a shared pointer type for each of these PB types
newcoin::FullLedger* Ledger::createFullLedger()
{	
	newcoin::FullLedger* ledger=new newcoin::FullLedger();
	ledger->set_index(mIndex);
	ledger->set_hash(getHash().begin(),getHash().GetSerializeSize());
	ledger->set_parenthash(mParentHash.begin(),mParentHash.GetSerializeSize());
	
	pair<uint160, pair<uint64,uint32> >& account=pair<uint160, pair<uint64,uint32> >();
	BOOST_FOREACH(account,mAccounts)
	{
		newcoin::Account* saveAccount=ledger->add_accounts();
		saveAccount->set_address(account.first.begin(),account.first.GetSerializeSize());
		saveAccount->set_amount(account.second.first);
		saveAccount->set_seqnum(account.second.second);
	}
	
	return(ledger);
}

void Ledger::setTo(newcoin::FullLedger& ledger)
{
	mIndex=ledger.index();
	mTransactions.clear();
	mDiscardedTransactions.clear();
	mAccounts.clear();
	mValidSig=false;
	mValidHash=false;

	mParentHash=Transaction::protobufToInternalHash(ledger.parenthash());	

	int numAccounts=ledger.accounts_size();
	for(int n=0; n<numAccounts; n++)
	{
		const newcoin::Account& account=ledger.accounts(n);
		mAccounts[ NewcoinAddress::protobufToInternal(account.address()) ] = Account(account.amount(),account.seqnum());
	}

	int numTrans=ledger.transactions_size();
	for(int n=0; n<numTrans; n++)
	{
		const newcoin::Transaction& trans=ledger.transactions(n);
		mTransactions.push_back(TransactionPtr(new newcoin::Transaction(trans)));
	}
}

Ledger::pointer Ledger::getParent()
{
	if(!mParent)
	{
		mParent=theApp->getLedgerMaster().getLedger(mParentHash);
	}
	return(mParent);
}

bool Ledger::load(uint256& hash)
{
	Database* db=theApp->getDB();

	string sql="SELECT * from Ledgers where hash=";
	string hashStr;
	db->escape(hash.begin(),hash.GetSerializeSize(),hashStr);
	sql.append(hashStr);

	if(db->executeSQL(sql.c_str()))
	{
		db->getNextRow();
		sql="SELECT * from Transactions where "
	}
	return(false);
}

/* 
bool Ledger::load(std::string dir)
{

	string filename=strprintf("%s%u.ledger",dir,mIndex);
	
	ifstream loadfile(filename, ios::in | ios::binary);
	if(loadfile.is_open()) // TODO: does this fail correctly?
	{
		newcoin::FullLedger ledger;
		ledger.ParseFromIstream(&loadfile);
		loadfile.close();
		setTo(ledger);
		return(true);
	}

	return(false);
}


void Ledger::save(string dir)
{ 
	string filename=strprintf("%s%u.ledger",dir,mIndex);

	newcoin::FullLedger* ledger=createFullLedger();
	ofstream savefile(filename, ios::out | ios::trunc | ios::binary);
	if(savefile.is_open()) 
	{
		ledger->SerializeToOstream(&savefile);
		savefile.close();
	}
	delete(ledger);
}
*/

int64 Ledger::getAmountHeld(uint160& address)
{
	if(mAccounts.count(address))
	{
		return(mAccounts[address].first);
	}
	return(0);
}

Ledger::Account* Ledger::getAccount(uint160& address)
{
	if(mAccounts.count(address))
	{
		return(&(mAccounts[address]));
	}
	return(NULL);
}

uint256& Ledger::getHash()
{
	if(!mValidHash) hash();
	return(mHash); 
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

void Ledger::sign()
{
	// TODO: Ledger::sign()
}


void Ledger::hash()
{
	// TODO: Ledger::hash()
}

/*
uint64 Ledger::getAmount(std::string address)
{
	return(mAccounts[NewcoinAddress:: address].first);
}*/

// returns true if the from account has enough for the transaction and seq num is correct
bool Ledger::addTransaction(TransactionPtr trans,bool checkDuplicate)
{
	if(checkDuplicate && hasTransaction(trans)) return(false);

	if(mParent)
	{ // check the lineage of the from addresses
		uint160 address=NewcoinAddress::protobufToInternal(trans->from());
		if(mAccounts.count(address))
		{
			pair<uint64,uint32> account=mAccounts[address];
			if( (account.first<trans->amount()) &&
				(trans->seqnum()==account.second) )
			{
				account.first -= trans->amount();
				account.second++;
				mAccounts[address]=account;


				uint160 destAddress=NewcoinAddress::protobufToInternal(trans->dest());

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
void Ledger::addTransactionAllowNeg(TransactionPtr trans)
{
	uint160 fromAddress=NewcoinAddress::protobufToInternal(trans->from());

	if(mAccounts.count(fromAddress))
	{
		Account fromAccount=mAccounts[fromAddress];
		if(trans->seqnum()==fromAccount.second) 
		{
			fromAccount.first -= trans->amount();
			fromAccount.second++;
			mAccounts[fromAddress]=fromAccount;
			
			uint160 destAddress=NewcoinAddress::protobufToInternal(trans->dest());

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

			uint160 destAddress=NewcoinAddress::protobufToInternal(trans->dest());

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
		list<TransactionPtr> firstTransactions=mTransactions;
		list<TransactionPtr> secondTransactions=mDiscardedTransactions;

		mTransactions.clear();
		mDiscardedTransactions.clear();

		firstTransactions.sort(gTransactionSorter);
		secondTransactions.sort(gTransactionSorter);

		// don't check balances until the end
		BOOST_FOREACH(TransactionPtr trans,firstTransactions)
		{
			addTransactionAllowNeg(trans);
		}

		BOOST_FOREACH(TransactionPtr trans,secondTransactions)
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



void Ledger::parentAddedTransaction(TransactionPtr cause)
{
	// TODO: we can make this more efficient at some point. For now just redo everything

	recalculate();

	/* 
	// IMPORTANT: these changes can't change the sequence number. This means we only need to check the dest account
	// If there was a seqnum change we have to re-do all the transactions again

	// There was a change to the balances of the parent ledger
	// This could cause:
	//		an account to now be negative so we have to discard one
	//		a discarded transaction to be pulled back in
	//		seqnum invalidation
	uint160 fromAddress=NewcoinAddress::protobufToInternal(cause->from());
	uint160 destAddress=NewcoinAddress::protobufToInternal(cause->dest());
	
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
	BOOST_FOREACH(TransactionPtr trans,)
	*/
}

bool Ledger::hasTransaction(TransactionPtr needle)
{
	BOOST_FOREACH(TransactionPtr trans,mTransactions)
	{
		if( Transaction::isEqual(needle,trans) ) return(true);
	}

	BOOST_FOREACH(TransactionPtr disTrans,mDiscardedTransactions)
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
	list<TransactionPtr>& otherTransactions=other->getTransactions();
	BOOST_FOREACH(TransactionPtr trans,otherTransactions)
	{
		addTransactionAllowNeg(trans);
	}

	correctAccounts();
}

void Ledger::correctAccounts()
{
	pair<uint160, Account >& fullAccount=pair<uint160, Account >();
	BOOST_FOREACH(fullAccount,mAccounts)
	{
		if(fullAccount.second.first <0 )
		{
			correctAccount(fullAccount.first);
		}
	}
}

// Must look for transactions to discard to make this account positive
// When we chuck transactions it might cause other accounts to need correcting
void Ledger::correctAccount(uint160& address)
{
	list<uint160> effected;

	// do this in reverse so we take of the higher seqnum first
	for( list<TransactionPtr>::reverse_iterator iter=mTransactions.rbegin(); iter != mTransactions.rend(); )
	{
		TransactionPtr trans= *iter;
		if(NewcoinAddress::protobufToInternal(trans->from()) == address)
		{
			Account fromAccount=mAccounts[address];
			assert(fromAccount.second==trans->seqnum()+1);
			if(fromAccount.first<0)
			{
				fromAccount.first += trans->amount();
				fromAccount.second --;

				mAccounts[address]=fromAccount;

				uint160 destAddress=NewcoinAddress::protobufToInternal(trans->dest());
				Account destAccount=mAccounts[destAddress];
				destAccount.first -= trans->amount();
				mAccounts[destAddress]=destAccount;
				if(destAccount.first<0) effected.push_back(destAddress);

				list<TransactionPtr>::iterator temp=mTransactions.erase( --iter.base() );
				if(fromAccount.first>=0) break; 

				iter=list<TransactionPtr>::reverse_iterator(temp);
			}else break;	
		}else iter--;
	}

	BOOST_FOREACH(uint160& address,effected)
	{
		correctAccount(address);
	}

}