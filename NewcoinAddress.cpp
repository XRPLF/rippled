#include "NewcoinAddress.h"
#include "Config.h"
#include "BitcoinUtil.h"
#include <assert.h>



bool NewcoinAddress::SetHash160(const uint160& hash160)
{
	SetData(51, &hash160, 20);
	return true;
}

bool NewcoinAddress::SetPubKey(const std::vector<unsigned char>& vchPubKey)
{
	return SetHash160(Hash160(vchPubKey));
}

bool NewcoinAddress::IsValid() 
{
	return nVersion == 51 && vchData.size() == 20;
}

NewcoinAddress::NewcoinAddress()
{
}

NewcoinAddress::NewcoinAddress(uint160& hash160In)
{
	SetHash160(hash160In);
}

NewcoinAddress::NewcoinAddress(const std::vector<unsigned char>& vchPubKey)
{
	SetPubKey(vchPubKey);
}

NewcoinAddress::NewcoinAddress(const std::string& strAddress)
{
	SetString(strAddress);
}

NewcoinAddress::NewcoinAddress(const char* pszAddress)
{
	SetString(pszAddress);
}

uint160 NewcoinAddress::GetHash160()  const
{
	assert(vchData.size() == 20);
	uint160 hash160;
	memcpy(&hash160, &vchData[0], 20);
	return hash160;
}

