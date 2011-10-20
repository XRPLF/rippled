#ifndef __WALLET__
#define __WALLET__

#include "keystore.h"
#include "newcoin.pb.h"
#include "Transaction.h"
#include <list>
#include <vector>

class NewcoinAddress;

/*
Keeps track of all the public/private keys you have created
*/
class Wallet : public CBasicKeyStore
{
	class Account
	{
	public:
		//CKey mKey;
		//std::string mHumanAddress;
		uint160 mAddress;
		std::vector<unsigned char> mPublicKey;
		std::vector<unsigned char> mPrivateKey;
		int64 mAmount;
		uint64 mAge; // do we need this
		uint32 mSeqNum;


		Account(){}
		bool signTransaction(TransactionPtr input);
	};
	std::list<Account> mYourAccounts;

	

	TransactionPtr createTransaction(Account& fromAccount, uint160& destAddr, int64 amount);
	bool commitTransaction(TransactionPtr trans);

	Account* consolidateAccountOfSize(int64 amount);

public:
	Wallet();
	void refreshAccounts();
	void load();

	int64 getBalance();

	// returns some human error str?
	std::string sendMoneyToAddress(uint160& destAddress, int64 amount);

	// you may need to update your balances
	void transactionChanged(TransactionPtr trans);

	
};

#endif