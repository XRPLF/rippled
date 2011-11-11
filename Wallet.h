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
class LocalAccount
{
public:
	//CKey mKey;
	//std::string mHumanAddress;
	uint160 mAddress;
	CKey mPublicKey, mPrivateKey;
	int64 mAmount;
	uint32 mSeqNum;

	bool SignRaw(const std::vector<unsigned char> &toSign, std::vector<unsigned char> &signature);
	bool CheckSignRaw(const std::vector<unsigned char> &toSign, const std::vector<unsigned char> &signature);
};

class Wallet : public CBasicKeyStore
{
	std::list<LocalAccount> mYourAccounts;

	

	TransactionPtr createTransaction(LocalAccount& fromAccount, uint160& destAddr, int64 amount);
	bool commitTransaction(TransactionPtr trans);

	LocalAccount* consolidateAccountOfSize(int64 amount);

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