#include "Wallet.h"
#include "NewcoinAddress.h"
#include "Application.h"
#include "LedgerMaster.h"
//#include "script.h"
#include <string>
#include <boost/foreach.hpp>
using namespace std;

Wallet::Wallet()
{

}

void Wallet::load()
{

}

int64 Wallet::getBalance()
{
	int64 total = 0;

	LedgerMaster& ledgerMaster=theApp->getLedgerMaster();

	BOOST_FOREACH(Account& account, mYourAccounts)
	{
		total += ledgerMaster.getAmountHeld(account.mAddress);
	}

	return total;
}

void Wallet::refreshAccounts()
{
	LedgerMaster& ledgerMaster=theApp->getLedgerMaster();

	BOOST_FOREACH(Account& account, mYourAccounts)
	{
		Ledger::Account* ledgerAccount=ledgerMaster.getAccount(account.mAddress);
		if(ledgerAccount)
		{
			account.mAmount= ledgerAccount->first;
			account.mSeqNum= ledgerAccount->second;
		}else
		{
			account.mAmount=0;
			account.mSeqNum=0;
		}
	}
}

void Wallet::transactionChanged(TransactionPtr trans)
{ 

	BOOST_FOREACH(Account& account, mYourAccounts)
	{
		if( account.mAddress == NewcoinAddress::protobufToInternal(trans->from()) ||
			account.mAddress == NewcoinAddress::protobufToInternal(trans->dest()) )
		{
			Ledger::Account* ledgerAccount=theApp->getLedgerMaster().getAccount(account.mAddress);
			if(ledgerAccount)
			{
				account.mAmount= ledgerAccount->first;
				account.mSeqNum= ledgerAccount->second;
			}else
			{
				account.mAmount=0;
				account.mSeqNum=0;
			}
		}
	}
}

Wallet::Account* Wallet::consolidateAccountOfSize(int64 amount)
{
	int64 total=0;
	BOOST_FOREACH(Account& account, mYourAccounts)
	{
		if(account.mAmount>=amount) return(&account);
		total += account.mAmount;
	}
	if(total<amount) return(NULL);

	Account* firstAccount=NULL;
	uint160* firstAddr=NULL;
	total=0;
	BOOST_FOREACH(Account& account, mYourAccounts)
	{
		total += account.mAmount;
		if(firstAccount) 
		{
			TransactionPtr trans=createTransaction(account,firstAccount->mAddress,account.mAmount);
			commitTransaction(trans);
		}else firstAccount=&account;

		if(total>=amount) return(firstAccount);
	}

	assert(0);
	return(NULL);
}

string Wallet::sendMoneyToAddress(uint160& destAddress, int64 amount)
{
	// we may have to bundle up money in order to send this amount
	Account* fromAccount=consolidateAccountOfSize(amount);
	if(fromAccount)
	{
		TransactionPtr trans=createTransaction(*fromAccount,destAddress,amount);
		if(trans)
		{
			if(!commitTransaction(trans))
				return("Error: The transaction was rejected.  This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.");

		}
	}else return("Insufficient funds");

	return "";
}

TransactionPtr Wallet::createTransaction(Account& fromAccount, uint160& destAddr, int64 amount)
{
	TransactionPtr trans(new newcoin::Transaction());
	trans->set_amount(amount);
	trans->set_seqnum(fromAccount.mSeqNum);
	trans->set_from(fromAccount.mAddress.begin(), fromAccount.mAddress.GetSerializeSize());
	trans->set_dest(destAddr.begin(),destAddr.GetSerializeSize());
	trans->set_ledgerindex(theApp->getLedgerMaster().getCurrentLedgerIndex());
	// TODO: trans->set_pubkey(fromAccount.mPublicKey);
	fromAccount.signTransaction(trans);

	return(trans);
}




bool Wallet::Account::signTransaction(TransactionPtr trans)
{
	/* TODO:
	uint256 hash = Transaction::calcHash(trans);

	CKey key;
	if(!GetKey(input.from(), key))
		return false;

	if(hash != 0)
	{
		vector<unsigned char> vchSig;
		if(!key.Sign(hash, retSig))
			return false;
	}
	*/
	return(true);
}


bool Wallet::commitTransaction(TransactionPtr trans)
{
	if(trans)
	{
		if(theApp->getLedgerMaster().addTransaction(trans))
		{
			ConnectionPool& pool=theApp->getConnectionPool();
			PackedMessage::pointer packet(new PackedMessage(PackedMessage::MessagePointer(new newcoin::Transaction(*(trans.get()))),newcoin::TRANSACTION));
			pool.relayMessage(NULL,packet);

		}else cout << "Problem adding the transaction to your local ledger" << endl;
	}
	return(false);
}
