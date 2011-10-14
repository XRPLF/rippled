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


Ledger::Ledger(uint64 index)
{
	mIndex=index;
	mValidSig=false;
	mValidHash=false;
}

// TODO: we should probably make a shared pointer type for each of these PB types
newcoin::FullLedger* Ledger::createFullLedger()
{
	// TODO: do we need to hash and create mone map first?
	newcoin::FullLedger* ledger=new newcoin::FullLedger();
	ledger->set_index(mIndex);
	ledger->set_hash(mHash);
	
	pair<string,uint64>& account=pair<string,uint64>();
	BOOST_FOREACH(account,mMoneyMap)
	{
		newcoin::Account* saveAccount=ledger->add_accounts();
		saveAccount->set_address(account.first);
		saveAccount->set_amount(account.second);
	}
	mBundle.addTransactionsToPB(ledger);
	
	return(ledger);
}

void Ledger::setTo(newcoin::FullLedger& ledger)
{
	mIndex=ledger.index();
	mBundle.clear();
	mMoneyMap.clear();
	mValidSig=false;
	mValidHash=false;
	
	int numAccounts=ledger.accounts_size();
	for(int n=0; n<numAccounts; n++)
	{
		const newcoin::Account& account=ledger.accounts(n);
		mMoneyMap[account.address()] = account.amount();
	}

	int numTrans=ledger.transactions_size();
	for(int n=0; n<numTrans; n++)
	{
		const newcoin::Transaction& trans=ledger.transactions(n);
		mBundle.addTransaction(trans);
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
	theApp->getConnectionPool().relayMessage(NULL,packet,mIndex);
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
	// start with map from the previous ledger
	// go through every transaction
	Ledger::pointer parent=theApp->getLedgerMaster().getLedger(mIndex-1);
	if(parent)
	{
		mMoneyMap.clear();
		mMoneyMap=parent->getMoneyMap();

		mBundle.updateMap(mMoneyMap);
		// TODO: strip the 0 ones
	}
}

void Ledger::hash()
{
	calcMoneyMap();

	// TODO:
}

uint64 Ledger::getAmount(std::string address)
{
	return(mMoneyMap[address]);
}

// returns true if the transaction was valid
bool Ledger::addTransaction(newcoin::Transaction& trans)
{
	if(mBundle.hasTransaction(trans)) return(false);

	Ledger::pointer parent=theApp->getLedgerMaster().getLedger(mIndex-1);
	if(parent)
	{ // check the lineage of the from addresses
		vector<uint64> cacheInputLeftOverAmount;
		int numInputs=trans.inputs_size();
		cacheInputLeftOverAmount.resize(numInputs);
		for(int n=0; n<numInputs; n++)
		{
			const newcoin::TransInput& input=trans.inputs(n);

			uint64 amountHeld=parent->getAmount(input.from());
			// TODO: checkValid could invalidate mValidSig and mValidHash
			amountHeld = mBundle.checkValid(input.from(),amountHeld,0,trans.seconds());
			if(amountHeld<input.amount())
			{
				mBundle.addDiscardedTransaction(trans);
				cout << "Not enough money" << endl;
				return(false);
			}
			cacheInputLeftOverAmount[n]=amountHeld-input.amount();
		}

		for(int n=0; n<numInputs; n++)
		{
			const newcoin::TransInput& input=trans.inputs(n);
			mBundle.checkValid(input.from(),cacheInputLeftOverAmount[n],trans.seconds(),theConfig.LEDGER_SECONDS);
		}
		
		mValidSig=false;
		mValidHash=false;
		mBundle.addTransaction(trans);
		
		
	}else
	{ // we have no way to know so just accept it
		mValidSig=false;
		mValidHash=false;
		mBundle.addTransaction(trans);
		return(true);
	}


	return(false);
}

void Ledger::recheck(Ledger::pointer parent,newcoin::Transaction& cause)
{
	// TODO: 
}