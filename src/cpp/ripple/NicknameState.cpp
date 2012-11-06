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
	? mLedgerEntry->getFieldAmount(sfMinimumOffer)
	: STAmount();
}

RippleAddress NicknameState::getAccountID() const
{
    return mLedgerEntry->getFieldAccount(sfAccount);
}

void NicknameState::addJson(Json::Value& val)
{
    val = mLedgerEntry->getJson(0);
}
