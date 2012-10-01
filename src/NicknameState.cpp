#include "NicknameState.h"

NicknameState::NicknameState(SerializedLedgerEntry::pointer ledgerEntry) :
    mLedgerEntry(ledgerEntry)
{
    if (!mLedgerEntry || mLedgerEntry->getType() != ltNICKNAME) return;
}

bool NicknameState::haveMinimumOffer() const
{
    return mLedgerEntry->isFieldPresent(sfMinimumOffer);
}

STAmount NicknameState::getMinimumOffer() const
{
    return mLedgerEntry->isFieldPresent(sfMinimumOffer)
	? mLedgerEntry->getValueFieldAmount(sfMinimumOffer)
	: STAmount();
}

NewcoinAddress NicknameState::getAccountID() const
{
    return mLedgerEntry->getValueFieldAccount(sfAccount);
}

void NicknameState::addJson(Json::Value& val)
{
    val = mLedgerEntry->getJson(0);
}
