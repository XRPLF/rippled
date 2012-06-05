#include "NicknameState.h"

NicknameState::NicknameState(SerializedLedgerEntry::pointer ledgerEntry) :
    mLedgerEntry(ledgerEntry)
{
    if (!mLedgerEntry || mLedgerEntry->getType() != ltNICKNAME) return;
}

bool NicknameState::haveMinimumOffer() const
{
    return mLedgerEntry->getIFieldPresent(sfMinimumOffer);
}

STAmount NicknameState::getMinimumOffer() const
{
    return mLedgerEntry->getIFieldPresent(sfMinimumOffer)
	? mLedgerEntry->getIValueFieldAmount(sfMinimumOffer)
	: STAmount();
}

NewcoinAddress NicknameState::getAccountID() const
{
    return mLedgerEntry->getIValueFieldAccount(sfAccount);
}

void NicknameState::addJson(Json::Value& val)
{
    val = mLedgerEntry->getJson(0);
}
