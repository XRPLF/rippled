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

uint64 Wallet::getBalance()
{
	uint64 total = 0;

	LedgerMaster& ledgerMaster=theApp->getLedgerMaster();

	BOOST_FOREACH(Account& account, mYourAccounts)
	{
		total += ledgerMaster.getAmountHeld(account.mAddress);
	}

	return total;
}

string Wallet::sendMoneyToAddress(NewcoinAddress& destAddress, uint64 amount)
{
	// Check amount
	if(amount > getBalance())
		return("Insufficient funds");

	newcoin::Transaction trans;
	if(!createTransaction(destAddress, amount, trans))
	{
		return "Error: Transaction creation failed  ";
	}

	if(!commitTransaction(trans))
		return("Error: The transaction was rejected.  This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.");


	return "";
}


bool Wallet::createTransaction(NewcoinAddress& destAddress, uint64 amount,newcoin::Transaction& trans)
{
	// find accounts to send from
	// sign each account that is sending

	
	trans.set_ledgerindex(theApp->getLedgerMaster().getCurrentLedgerIndex());
	trans.set_seconds(theApp->getLedgerMaster().getCurrentLedgerSeconds());
	trans.set_dest(destAddress.ToString());

	list<newcoin::TransInput*> inputs;
	BOOST_FOREACH(Account& account, mYourAccounts)
	{
		newcoin::TransInput* input=trans.add_inputs();
		inputs.push_back(input);
		input->set_from(account.mAddress);

		if(account.mAmount < amount)
		{	// this account can only fill a portion of the amount
			input->set_amount(account.mAmount);
			amount -= account.mAmount;
			
		}else
		{	// this account can fill the whole thing
			input->set_amount(amount);
			
			break;
		}
	}
	
	uint256 hash = calcTransactionHash(trans);
	BOOST_FOREACH(newcoin::TransInput* input,inputs)
	{
		vector<unsigned char> sig;
		if(signTransInput(hash,*input,sig))
			input->set_sig(&(sig[0]),sig.size());
		else return(false);
	}
	trans.set_transid(hash.ToString());
	return(true);
}

uint256 Wallet::calcTransactionHash(newcoin::Transaction& trans)
{
	vector<unsigned char> buffer;
	buffer.resize(trans.ByteSize());
	trans.SerializeToArray(&(buffer[0]),buffer.size());
	return Hash(buffer.begin(), buffer.end());
}


bool Wallet::signTransInput(uint256 hash, newcoin::TransInput& input,vector<unsigned char>& retSig)
{
	CKey key;
	if(!GetKey(input.from(), key))
		return false;

	if(hash != 0)
	{
		vector<unsigned char> vchSig;
		if(!key.Sign(hash, retSig))
			return false;
	}
	return(true);
}





// Call after CreateTransaction unless you want to abort
bool Wallet::commitTransaction(newcoin::Transaction& trans)
{
	// TODO: Q up the message if it can't be relayed properly. or we don't see it added.
	PackedMessage::pointer msg(new PackedMessage(PackedMessage::MessagePointer(new newcoin::Transaction(trans)),newcoin::TRANSACTION));
	theApp->getConnectionPool().relayMessage(NULL,msg,trans.ledgerindex());
	theApp->getLedgerMaster().addTransaction(trans);

	return true;
}
