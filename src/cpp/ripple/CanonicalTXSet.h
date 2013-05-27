#ifndef __CANONICAL_TX_SET_
#define __CANONICAL_TX_SET_

#include <map>

#include "SerializedTransaction.h"

class CanonicalTXKey
{
protected:
	uint256 mAccount, mTXid;
	uint32 mSeq;

public:
	CanonicalTXKey(const uint256& account, uint32 seq, const uint256& id)
		: mAccount(account), mTXid(id), mSeq(seq) { ; }

	bool operator<(const CanonicalTXKey&) const;
	bool operator>(const CanonicalTXKey&) const;
	bool operator<=(const CanonicalTXKey&) const;
	bool operator>=(const CanonicalTXKey&) const;

	bool operator==(const CanonicalTXKey& k) const	{ return mTXid == k.mTXid; }
	bool operator!=(const CanonicalTXKey& k) const	{ return mTXid != k.mTXid; }

	const uint256& getTXID() const					{ return mTXid; }
};

class CanonicalTXSet
{
public:
	typedef std::map<CanonicalTXKey, SerializedTransaction::pointer>::iterator iterator;
	typedef std::map<CanonicalTXKey, SerializedTransaction::pointer>::const_iterator const_iterator;

protected:
	uint256 mSetHash;
	std::map<CanonicalTXKey, SerializedTransaction::pointer> mMap;

public:
	CanonicalTXSet(const uint256& lclHash) : mSetHash(lclHash) { ; }

	void push_back(SerializedTransaction::ref txn);

	void reset(const uint256& newLCL)
	{
		mSetHash = newLCL;
		mMap.clear();
	}

	iterator erase(const iterator& it);

	iterator begin()				{ return mMap.begin(); }
	iterator end()					{ return mMap.end(); }
	const_iterator begin()	const	{ return mMap.begin(); }
	const_iterator end() const		{ return mMap.end(); }
	size_t size() const				{ return mMap.size(); }
	bool empty() const				{ return mMap.empty(); }
};

#endif
