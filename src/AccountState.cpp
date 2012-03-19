
#include <boost/lexical_cast.hpp>

#include "AccountState.h"
#include "Serializer.h"

AccountState::AccountState(const std::vector<unsigned char>& v)
{
	Serializer s(v);
	mValid=false;
	uint160	acct160	= mAccountID.getAccountID();

	if(!s.get160(acct160, 0)) { assert(false); return; }
	if(!s.get64(mBalance, 20)) { assert(false); return; }
	if(!s.get32(mAccountSeq, 28)) { assert(false); return; }
	mValid=true;
}

AccountState::AccountState(const NewcoinAddress& id) : mAccountID(id), mBalance(0), mAccountSeq(0), mValid(true)
{ ; }

std::vector<unsigned char> AccountState::getRaw() const
{ // 20-byte acct ID, 8-byte balance, 4-byte sequence
	Serializer s(32);
	s.add160(mAccountID.getAccountID());
	s.add64(mBalance);
	s.add32(mAccountSeq);
	return s.getData();
}

void AccountState::addJson(Json::Value& val)
{
	Json::Value as(Json::objectValue);

	as["AccountID"]=mAccountID.humanAccountID();
	as["Balance"]=boost::lexical_cast<std::string>(mBalance);
	as["SendSequence"]=mAccountSeq;
	if(!mValid) as["Invalid"]=true;

	val[mAccountID.humanAccountID()]=as;
}
// vim:ts=4
