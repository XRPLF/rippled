#ifndef __NEWCOIN_ADDRESS__
#define __NEWCOIN_ADDRESS__

#include "base58.h"


class NewcoinAddress : public CBase58Data
{
public:
	NewcoinAddress();
	NewcoinAddress(uint160& hash160In);
	NewcoinAddress(const std::vector<unsigned char>& vchPubKey);
	NewcoinAddress(const std::string& strAddress);
	NewcoinAddress(const char* pszAddress);

	bool SetHash160(const uint160& hash160);
	bool SetPubKey(const std::vector<unsigned char>& vchPubKey);
	

	bool IsValid();

	uint160 GetHash160();

	static uint160 protobufToInternal(const std::string& buf);
	static uint160 humanToInternal(const std::string& buf);

	
};

#endif
