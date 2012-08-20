
#include "CanonicalTXSet.h"

bool CanonicalTXKey::operator<(const CanonicalTXKey& key) const
{
	if (mAccount < key.mAccount) return true;
	if (mAccount > key.mAccount) return false;
	if (mSeq < key.mSeq) return true;
	if (mSeq > key.mSeq) return false;
	return mTXid < key.mTXid;
}

bool CanonicalTXKey::operator>(const CanonicalTXKey& key) const
{
	if (mAccount > key.mAccount) return true;
	if (mAccount < key.mAccount) return false;
	if (mSeq > key.mSeq) return true;
	if (mSeq < key.mSeq) return false;
	return mTXid > key.mTXid;
}

bool CanonicalTXKey::operator<=(const CanonicalTXKey& key) const
{
	if (mAccount < key.mAccount) return true;
	if (mAccount > key.mAccount) return false;
	if (mSeq < key.mSeq) return true;
	if (mSeq > key.mSeq) return false;
	return mTXid <= key.mTXid;
}

bool CanonicalTXKey::operator>=(const CanonicalTXKey& key)const
{
	if (mAccount > key.mAccount) return true;
	if (mAccount < key.mAccount) return false;
	if (mSeq > key.mSeq) return true;
	if (mSeq < key.mSeq) return false;
	return mTXid >= key.mTXid;
}

void CanonicalTXSet::push_back(const SerializedTransaction::pointer& txn)
{
	uint256 effectiveAccount = mSetHash;
	effectiveAccount ^= txn->getSourceAccount().getAccountID().to256();
	mMap.insert(std::make_pair(CanonicalTXKey(effectiveAccount, txn->getSequence(), txn->getTransactionID()), txn));
}

CanonicalTXSet::iterator CanonicalTXSet::erase(const iterator& it)
{
	iterator tmp = it;
	++tmp;
	mMap.erase(it);
	return tmp;
}


