
#include <boost/lexical_cast.hpp>

#include "AccountState.h"
#include "Serializer.h"

AccountState::AccountState(const std::vector<unsigned char>& v)
{
	Serializer s(v);
	mValid=false;
	if(!s.get160(mAccountID, 0)) { assert(false); return; }
	if(!s.get64(mBalance, 20)) { assert(false); return; }
	if(!s.get32(mAccountSeq, 28)) { assert(false); return; }
	mValid=true;
}

AccountState::AccountState(const uint160& id) : mAccountID(id), mBalance(0), mAccountSeq(0), mValid(true)
{ ; }

std::vector<unsigned char> AccountState::getRaw() const
{ // 20-byte acct ID, 8-byte balance, 4-byte sequence
	Serializer s(32);
	s.add160(mAccountID);
	s.add64(mBalance);
	s.add32(mAccountSeq);
	return s.getData();
}

static bool isHex(char j)
{
	if((j>='0') && (j<='9')) return true;
	if((j>='A') && (j<='F')) return true;
	if((j>='a') && (j<='f')) return true;
	return false;
}

bool AccountState::isHexAccountID(const std::string& acct)
{
	if(acct.size()!=40) return false;
	for(int i=1; i<40; i++)
		if(!isHex(acct[i])) return false;
	 return true;
}

void AccountState::addJson(Json::Value& val)
{
	Json::Value as(Json::objectValue);
	as["Account"]=mAccountID.GetHex();
	as["Balance"]=boost::lexical_cast<std::string>(mBalance);
	as["SendSequence"]=mAccountSeq;
	if(!mValid) as["Invalid"]=true;
	NewcoinAddress nad(mAccountID);
	val[nad.GetString()]=as;
}
