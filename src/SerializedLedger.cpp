#include "SerializedLedger.h"

#include <boost/format.hpp>

#include "Ledger.h"

SerializedLedgerEntry::SerializedLedgerEntry(SerializerIterator& sit, const uint256& index)
	: SerializedType("LedgerEntry"), mIndex(index)
{
	uint16 type = sit.get16();
	mFormat = getLgrFormat(static_cast<LedgerEntryType>(type));
	if (mFormat == NULL) throw std::runtime_error("invalid ledger entry type");
	mType = mFormat->t_type;
	mVersion.setValue(type);
	mObject = STObject(mFormat->elements, sit);
}

SerializedLedgerEntry::SerializedLedgerEntry(const Serializer& s, const uint256& index)
	: SerializedType("LedgerEntry"), mIndex(index)
{
	SerializerIterator sit(s);

	uint16 type = sit.get16();
	mFormat = getLgrFormat(static_cast<LedgerEntryType>(type));
	if (mFormat == NULL) throw std::runtime_error("invalid ledger entry type");
	mType = mFormat->t_type;
	mVersion.setValue(type);
	mObject.set(mFormat->elements, sit);
}

SerializedLedgerEntry::SerializedLedgerEntry(LedgerEntryType type) : SerializedType("LedgerEntry"), mType(type)
{
	mFormat = getLgrFormat(type);
	if (mFormat == NULL) throw std::runtime_error("invalid ledger entry type");
	mVersion.setValue(static_cast<uint16>(mFormat->t_type));
	mObject.set(mFormat->elements);
}

std::string SerializedLedgerEntry::getFullText() const
{
	std::string ret = "\"";
	ret += mIndex.GetHex();
	ret += "\" = { ";
	ret += mFormat->t_name;
	ret += ", ";
	ret += mObject.getFullText();
	ret += "}";
	return ret;
}

std::string SerializedLedgerEntry::getText() const
{
	return str(boost::format("{ %s, %s, %s }")
		% mIndex.GetHex()
		% mVersion.getText()
		% mObject.getText());
}

Json::Value SerializedLedgerEntry::getJson(int options) const
{
	Json::Value ret(mObject.getJson(options));

	ret["type"]		= mFormat->t_name;
	ret["index"]	= mIndex.GetHex();
	ret["version"]	= std::string(1, mVersion);

	return ret;
}

bool SerializedLedgerEntry::isEquivalent(const SerializedType& t) const
{ // locators are not compared
	const SerializedLedgerEntry* v = dynamic_cast<const SerializedLedgerEntry*>(&t);
	if (!v) return false;
	if (mType != v->mType) return false;
	if (mObject != v->mObject) return false;
	return true;
}

bool SerializedLedgerEntry::isThreadedType()
{
	return getIFieldIndex(sfLastTxnID) != -1;
}

bool SerializedLedgerEntry::isThreaded()
{
	return getIFieldPresent(sfLastTxnID);
}

uint256 SerializedLedgerEntry::getThreadedTransaction()
{
	return getIFieldH256(sfLastTxnID);
}

uint32 SerializedLedgerEntry::getThreadedLedger()
{
	return getIFieldU32(sfLastTxnSeq);
}

bool SerializedLedgerEntry::thread(const uint256& txID, uint32 ledgerSeq, uint256& prevTxID, uint32& prevLedgerID)
{
	uint256 oldPrevTxID = getIFieldH256(sfLastTxnID);
	if (oldPrevTxID == txID)
		return false;
	prevTxID = oldPrevTxID;
	prevLedgerID = getIFieldU32(sfLastTxnSeq);
	assert(prevTxID != txID);
	setIFieldH256(sfLastTxnID, txID);
	setIFieldU32(sfLastTxnSeq, ledgerSeq);
	return true;
}

bool SerializedLedgerEntry::hasOneOwner()
{
	return (mType != ltACCOUNT_ROOT) && (getIFieldIndex(sfAccount) != -1);
}

bool SerializedLedgerEntry::hasTwoOwners()
{
	return mType == ltRIPPLE_STATE;
}

NewcoinAddress SerializedLedgerEntry::getOwner()
{
	return getIValueFieldAccount(sfAccount);
}

NewcoinAddress SerializedLedgerEntry::getFirstOwner()
{
	return getIValueFieldAccount(sfLowID);
}

NewcoinAddress SerializedLedgerEntry::getSecondOwner()
{
	return getIValueFieldAccount(sfHighID);
}

std::vector<uint256> SerializedLedgerEntry::getOwners()
{
	std::vector<uint256> owners;
	uint160 account;

	for (int i = 0, fields = getIFieldCount(); i < fields; ++i)
	{
		switch (getIFieldSType(i))
		{
			case sfAccount:
			case sfLowID:
			case sfHighID:
			{
				const STAccount* entry = dynamic_cast<const STAccount *>(mObject.peekAtPIndex(i));
				if ((entry != NULL) && entry->getValueH160(account))
					owners.push_back(Ledger::getAccountRootIndex(account));
			}

			default:
				nothing();
		}
	}

	return owners;
}

// vim:ts=4
