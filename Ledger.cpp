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


Ledger::Ledger(uint32 index)
{
	mIndex=index;
	mValidSig=false;
	mValidHash=false;
}

// TODO: we should probably make a shared pointer type for each of these PB types
newcoin::FullLedger* Ledger::createFullLedger()
{
	// TODO: do we need to hash and create accounts map first?
	newcoin::FullLedger* ledger=new newcoin::FullLedger();
	ledger->set_index(mIndex);
	ledger->set_hash(mHash);
	
	pair<uint160, pair<uint64,uint32> >& account=pair<uint160, pair<uint64,uint32> >();
	BOOST_FOREACH(account,mAccounts)
	{
		newcoin::Account* saveAccount=ledger->add_accounts();
		saveAccount->set_address(account.first.begin(),account.first.GetSerializeSize());
		saveAccount->set_amount(account.second.first);
		saveAccount->set_seqnum(account.second.second);
	}
	//mBundle.addTransactionsToPB(ledger);
	
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
	
	int numAccounts=ledger.accounts_size();
	for(int n=0; n<numAccounts; n++)
	{
		const newcoin::Account& account=ledger.accounts(n);
		mAccounts[ NewcoinAddress::protobufToInternal(account.address()) ] = pair<uint64,uint32>(account.amount(),account.seqnum());
	}

	int numTrans=ledger.transactions_size();
	for(int n=0; n<numTrans; n++)
	{
		const newcoin::Transaction& trans=ledger.transactions(n);
		mTransactions.push_back(TransactionPtr(new newcoin::Transaction(trans)));
	}
}

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

string& Ledger::getHash()
{
	if(!mValidHash) hash();
	return(mHash); 
}

string& Ledger::getSignature()
{
	if(!mValidSig) sign();
	return(mSignature); 
}

void Ledger::publish()
{
	PackedMessage::pointer packet=Peer::createValidation(shared_from_this());
	theApp->getConnectionPool().relayMessage(NULL,packet);
}

void Ledger::finalize()
{
	
}

void Ledger::sign()
{
	// TODO:
}

void Ledger::calcMoneyMap()
{
/*
	// start with map from the previous ledger
	// go through every transaction
	Ledger::pointer parent=theApp->getLedgerMaster().getLedger(mIndex-1);
	if(parent)
	{
		mMoneyMap.clear();
		mMoneyMap=parent->getMoneyMap();

		mBundle.updateMap(mMoneyMap);
		// TODO: strip the 0 ones
	} */
}

void Ledger::hash()
{
	calcMoneyMap();

	// TODO:
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
void Ledger::addTransactionRecalculate(TransactionPtr trans)
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
			addTransactionRecalculate(trans);
		}

		BOOST_FOREACH(TransactionPtr trans,secondTransactions)
		{
			addTransactionRecalculate(trans);
		}

		pair<uint160, Account >& fullAccount=pair<uint160, Account >();
		BOOST_FOREACH(fullAccount,mAccounts)
		{
			if(fullAccount.second.first <0 )
			{
				correctAccount(fullAccount.first);
			}
		}


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