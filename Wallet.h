#ifndef __WALLET__
#define __WALLET__

#include "keystore.h"
#include "newcoin.pb.h"
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
		std::string mAddress;
		//std::vector<unsigned char> mPublicKey;
		//std::vector<unsigned char> mPrivateKey;
		uint64 mAmount;
		uint64 mAge;
		Account(){}
	};
	std::list<Account> mYourAccounts;

	bool signTransInput(uint256 hash, newcoin::TransInput& input,std::vector<unsigned char>& retSig);
	uint256 calcTransactionHash(newcoin::Transaction& trans);

	bool createTransaction(NewcoinAddress& destAddress, uint64 amount,newcoin::Transaction& trans);
	bool commitTransaction(newcoin::Transaction& trans);
public:
	Wallet();
	void load();

	uint64 getBalance();

	// returns some human error str?
	std::string sendMoneyToAddress(NewcoinAddress& destAddress, uint64 amount);
};

#endif