#include "LedgerEntrySet.h"

#include <boost/make_shared.hpp>

void LedgerEntrySet::init(const uint256& transactionID, uint32 ledgerID)
{
	mEntries.clear();
	mSet.init(transactionID, ledgerID);
	mSeq = 0;
}

void LedgerEntrySet::clear()
{
	mEntries.clear();
	mSet.clear();
}

LedgerEntrySet LedgerEntrySet::duplicate() const
{
	return LedgerEntrySet(mEntries, mSet, mSeq + 1);
}

void LedgerEntrySet::setTo(LedgerEntrySet& e)
{
	mEntries = e.mEntries;
	mSet = e.mSet;
	mSeq = e.mSeq;
}

void LedgerEntrySet::swapWith(LedgerEntrySet& e)
{
	std::swap(mSeq, e.mSeq);
	mSet.swap(e.mSet);
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

void LedgerEntrySet::entryCache(SLE::pointer& sle)
{
	boost::unordered_map<uint256, LedgerEntrySetEntry>::iterator it = mEntries.find(sle->getIndex());
	if (it == mEntries.end())
	{
		mEntries.insert(std::make_pair(sle->getIndex(), LedgerEntrySetEntry(sle, taaCACHED, mSeq)));
		return;
	}

	switch (it->second.mAction)
	{
		case taaCACHED:
			it->second.mSeq	    = mSeq;
			it->second.mEntry   = sle;
			return;

		default:
			throw std::runtime_error("Cache after modify/delete/create");
	}
}

void LedgerEntrySet::entryCreate(SLE::pointer& sle)
{
	boost::unordered_map<uint256, LedgerEntrySetEntry>::iterator it = mEntries.find(sle->getIndex());
	if (it == mEntries.end())
	{
		mEntries.insert(std::make_pair(sle->getIndex(), LedgerEntrySetEntry(sle, taaCREATE, mSeq)));
		return;
	}

	assert(it->second.mSeq == mSeq);

	switch (it->second.mAction)
	{
		case taaMODIFY:
			throw std::runtime_error("Create after modify");

		case taaDELETE:
			throw std::runtime_error("Create after delete"); // We could make this a modify

		case taaCREATE:
			throw std::runtime_error("Create after create"); // We could make this work

		case taaCACHED:
			throw std::runtime_error("Create after cache");

		default:
			throw std::runtime_error("Unknown taa");
	}
}

void LedgerEntrySet::entryModify(SLE::pointer& sle)
{
	boost::unordered_map<uint256, LedgerEntrySetEntry>::iterator it = mEntries.find(sle->getIndex());
	if (it == mEntries.end())
	{
		mEntries.insert(std::make_pair(sle->getIndex(), LedgerEntrySetEntry(sle, taaMODIFY, mSeq)));
		return;
	}

	assert(it->second.mSeq == mSeq);
	assert(*it->second.mEntry == *sle);

	switch (it->second.mAction)
	{
		case taaCACHED:
			it->second.mAction  = taaMODIFY;
			fallthru();
		case taaMODIFY:
			it->second.mSeq	    = mSeq;
			it->second.mEntry   = sle;
			break;

		case taaDELETE:
			throw std::runtime_error("Modify after delete");

		case taaCREATE:
			it->second.mSeq	    = mSeq;
			it->second.mEntry   = sle;
			break;

		default:
			throw std::runtime_error("Unknown taa");
	}
 }

void LedgerEntrySet::entryDelete(SLE::pointer& sle, bool unfunded)
{
	boost::unordered_map<uint256, LedgerEntrySetEntry>::iterator it = mEntries.find(sle->getIndex());
	if (it == mEntries.end())
	{
		mEntries.insert(std::make_pair(sle->getIndex(), LedgerEntrySetEntry(sle, taaDELETE, mSeq)));
		return;
	}

	assert(it->second.mSeq == mSeq);
	assert(*it->second.mEntry == *sle);

	switch (it->second.mAction)
	{
		case taaCACHED:
		case taaMODIFY:
			it->second.mSeq	    = mSeq;
			it->second.mEntry   = sle;
			it->second.mAction  = taaDELETE;
			if (unfunded)
			{
				assert(sle->getType() == ltOFFER); // only offers can be unfunded
#if 0
				mSet.deleteUnfunded(sle->getIndex(),
					sle->getIValueFieldAmount(sfTakerPays),
					sle->getIValueFieldAmount(sfTakerGets));
#endif
			}
			break;

		case taaCREATE:
			mEntries.erase(it);
			break;

		case taaDELETE:
			break;

		default:
			throw std::runtime_error("Unknown taa");
	}
}

bool LedgerEntrySet::intersect(const LedgerEntrySet& lesLeft, const LedgerEntrySet& lesRight)
{
	return true;	// XXX Needs implementation
}

Json::Value LedgerEntrySet::getJson(int) const
{
	Json::Value ret(Json::objectValue);

	Json::Value nodes(Json::arrayValue);
	for (boost::unordered_map<uint256, LedgerEntrySetEntry>::const_iterator it = mEntries.begin(),
			end = mEntries.end(); it != end; ++it)
	{
		Json::Value entry(Json::objectValue);
		entry["node"] = it->first.GetHex();
		switch (it->second.mEntry->getType())
		{
			case ltINVALID:			entry["type"] = "invalid"; break;
			case ltACCOUNT_ROOT:	entry["type"] = "acccount_root"; break;
			case ltDIR_NODE:		entry["type"] = "dir_node"; break;
			case ltGENERATOR_MAP:	entry["type"] = "generator_map"; break;
			case ltRIPPLE_STATE:	entry["type"] = "ripple_state"; break;
			case ltNICKNAME:		entry["type"] = "nickname"; break;
			case ltOFFER:			entry["type"] = "offer"; break;
			default:				assert(false);
		}
		switch (it->second.mAction)
		{
			case taaCACHED:			entry["action"] = "cache"; break;
			case taaMODIFY:			entry["action"] = "modify"; break;
			case taaDELETE:			entry["action"] = "delete"; break;
			case taaCREATE:			entry["action"] = "create"; break;
			default:				assert(false);
		}
		nodes.append(entry);
	}
	ret["nodes" ] = nodes;

	return ret;
}

void LedgerEntrySet::addRawMeta(Serializer& s)
{
	for (boost::unordered_map<uint256, LedgerEntrySetEntry>::const_iterator it = mEntries.begin(),
			end = mEntries.end(); it != end; ++it)
	{
		switch (it->second.mAction)
		{
			case taaMODIFY:
				// WRITEME
				break;
			case taaDELETE:
				// WRITEME
				break;
			case taaCREATE:
				// WRITEME
				break;
			default:
				// ignore these
				break;
		}
	}
	mSet.addRaw(s);
}

// vim:ts=4
