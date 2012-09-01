
#include "AccountState.h"

#include <boost/lexical_cast.hpp>
#include <boost/make_shared.hpp>
#include <boost/algorithm/string.hpp>

#include "../json/writer.h"

#include "Ledger.h"
#include "Serializer.h"

AccountState::AccountState(const NewcoinAddress& naAccountID) : mAccountID(naAccountID), mValid(false)
{
	if (!naAccountID.isValid()) return;

	mLedgerEntry = boost::make_shared<SerializedLedgerEntry>(ltACCOUNT_ROOT);
	mLedgerEntry->setIndex(Ledger::getAccountRootIndex(naAccountID));
	mLedgerEntry->setIFieldAccount(sfAccount, naAccountID.getAccountID());

	mValid = true;
}

AccountState::AccountState(SLE::ref ledgerEntry, const NewcoinAddress& naAccountID) :
	mAccountID(naAccountID), mLedgerEntry(ledgerEntry), mValid(false)
{
	if (!mLedgerEntry)
		return;
	if (mLedgerEntry->getType() != ltACCOUNT_ROOT)
		return;

	mValid = true;
}

std::string AccountState::createGravatarUrl(uint128 uEmailHash)
{
	std::vector<unsigned char>	vucMD5(uEmailHash.begin(), uEmailHash.end());
	std::string					strMD5Lower	= strHex(vucMD5);
		boost::to_lower(strMD5Lower);

	return str(boost::format("http://www.gravatar.com/avatar/%s") % strMD5Lower);
}

void AccountState::addJson(Json::Value& val)
{
	val = mLedgerEntry->getJson(0);

	if (mValid)
	{
		if (mLedgerEntry->getIFieldPresent(sfEmailHash))
			val["UrlGravatar"]	= createGravatarUrl(mLedgerEntry->getIFieldH128(sfEmailHash));
	}
	else
	{
		val["Invalid"] = true;
	}
}

void AccountState::dump()
{
	Json::Value j(Json::objectValue);

	addJson(j);

	Json::StyledStreamWriter ssw;
	ssw.write(std::cerr, j);
}

// vim:ts=4
