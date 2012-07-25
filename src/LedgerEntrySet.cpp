#include "LedgerEntrySet.h"

LedgerEntrySet LedgerEntrySet::duplicate()
{
	return LedgerEntrySet(mEntries, mSeq + 1);
}

void LedgerEntrySet::setTo(LedgerEntrySet& e)
{
	mEntries = e.mEntries;
	mSeq = e.mSeq;
}

void LedgerEntrySet::swapWith(LedgerEntrySet& e)
{
	std::swap(mSeq, e.mSeq);
	mEntries.swap(e.mEntries);
}



