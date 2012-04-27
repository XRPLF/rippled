
#include "AccountState.h"

#include <boost/lexical_cast.hpp>
#include <boost/make_shared.hpp>

#include "../json/writer.h"

#include "Ledger.h"
#include "Serializer.h"

AccountState::AccountState(const NewcoinAddress& id) : mAccountID(id), mValid(false)
{
	if (!id.isValid()) return;
	mLedgerEntry = boost::make_shared<SerializedLedgerEntry>(ltACCOUNT_ROOT);
	mLedgerEntry->setIndex(Ledger::getAccountRootIndex(id));
	mLedgerEntry->setIFieldAccount(sfAccount, id);
	mValid = true;
}

AccountState::AccountState(SerializedLedgerEntry::pointer ledgerEntry) : mLedgerEntry(ledgerEntry), mValid(false)
{
	if (!mLedgerEntry) return;
	if (mLedgerEntry->getType() != ltACCOUNT_ROOT) return;
	mAccountID = mLedgerEntry->getIValueFieldAccount(sfAccount);
	if (mAccountID.isValid())
		mValid = true;
}

void AccountState::addJson(Json::Value& val)
{
	val = mLedgerEntry->getJson(0);
	if (!mValid) val["Invalid"] = true;
}

void AccountState::dump()
{
	Json::Value j(Json::objectValue);
	addJson(j);
	Json::StyledStreamWriter ssw;
	ssw.write(std::cerr, j);
}

// vim:ts=4
