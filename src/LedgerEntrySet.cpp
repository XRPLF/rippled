#include "LedgerEntrySet.h"

#include <boost/make_shared.hpp>

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

// Find an entry in the set.  If it has the wrong sequence number, copy it and update the sequence number.
// This is basically: copy-on-read.
SLE::pointer LedgerEntrySet::getEntry(const uint256& index, LedgerEntryAction& action)
{
	boost::unordered_map<uint256, LedgerEntrySetEntry>::iterator it = mEntries.find(index);
	if (it == mEntries.end())
	{
		action = taaNONE;
		return SLE::pointer();
	}
	if (it->second.mSeq != mSeq)
	{
		it->second.mEntry = boost::make_shared<SerializedLedgerEntry>(*it->second.mEntry);
		it->second.mSeq = mSeq;
	}
	action = it->second.mAction;
	return it->second.mEntry;
}

LedgerEntryAction LedgerEntrySet::hasEntry(const uint256& index) const
{
	boost::unordered_map<uint256, LedgerEntrySetEntry>::const_iterator it = mEntries.find(index);
	if (it == mEntries.end())
		return taaNONE;
	return it->second.mAction;
}

void LedgerEntrySet::entryCache(SLE::pointer sle)
{
	boost::unordered_map<uint256, LedgerEntrySetEntry>::iterator it = mEntries.find(sle->getIndex());
	if (it == mEntries.end())
		mEntries.insert(std::make_pair(sle->getIndex(), LedgerEntrySetEntry(sle, taaCACHED, mSeq)));
	else if (it->second.mAction == taaCACHED)
	{
		it->second.mSeq	    = mSeq;
		it->second.mEntry   = sle;
	}
	else
		throw std::runtime_error("Cache after modify/delete");
}

void LedgerEntrySet::entryCreate(SLE::pointer sle)
{
	boost::unordered_map<uint256, LedgerEntrySetEntry>::iterator it = mEntries.find(sle->getIndex());
	if (it == mEntries.end())
		mEntries.insert(std::make_pair(sle->getIndex(), LedgerEntrySetEntry(sle, taaCREATE, mSeq)));
	else if (it->second.mAction == taaDELETE)
		throw std::runtime_error("Create after delete"); // We could make this a modify
	else if (it->second.mAction == taaMODIFY)
		throw std::runtime_error("Create after modify");
	else
		throw std::runtime_error("Create after cache");
}

void LedgerEntrySet::entryModify(SLE::pointer sle)
{
	boost::unordered_map<uint256, LedgerEntrySetEntry>::iterator it = mEntries.find(sle->getIndex());
	if (it == mEntries.end())
		mEntries.insert(std::make_pair(sle->getIndex(), LedgerEntrySetEntry(sle, taaMODIFY, mSeq)));
	else if (it->second.mAction == taaDELETE)
		throw std::runtime_error("Modify after delete");
	else
	{
		it->second.mSeq	    = mSeq;
		it->second.mEntry   = sle;
		it->second.mAction  = (it->second.mAction == taaCREATE) ? taaCREATE : taaMODIFY;
	}
}

void LedgerEntrySet::entryDelete(SLE::pointer sle)
{
	boost::unordered_map<uint256, LedgerEntrySetEntry>::iterator it = mEntries.find(sle->getIndex());
	if (it == mEntries.end())
		mEntries.insert(std::make_pair(sle->getIndex(), LedgerEntrySetEntry(sle, taaDELETE, mSeq)));
	else if (it->second.mAction == taaCREATE) // We support delete after create
		mEntries.erase(it);
	else
	{
		it->second.mSeq	    = mSeq;
		it->second.mEntry   = sle;
		it->second.mAction  = taaDELETE;
	}
}

// vim:ts=4
