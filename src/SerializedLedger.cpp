#include "SerializedLedger.h"

#include <boost/format.hpp>

#include "Ledger.h"
#include "Log.h"

SerializedLedgerEntry::SerializedLedgerEntry(SerializerIterator& sit, const uint256& index)
	: STObject(sfLedgerEntry), mIndex(index)
{
	set(sit);
	uint16 type = getValueFieldU16(sfLedgerEntryType);
	mFormat = getLgrFormat(static_cast<LedgerEntryType>(type));
	if (mFormat == NULL)
		throw std::runtime_error("invalid ledger entry type");
	mType = mFormat->t_type;
	if (!setType(mFormat->elements))
		throw std::runtime_error("ledger entry not valid for type");
}

SerializedLedgerEntry::SerializedLedgerEntry(const Serializer& s, const uint256& index)
	: STObject(sfLedgerEntry), mIndex(index)
{
	SerializerIterator sit(s);
	set(sit);

	uint16 type = getValueFieldU16(sfLedgerEntryType);
	mFormat = getLgrFormat(static_cast<LedgerEntryType>(type));
	if (mFormat == NULL)
		throw std::runtime_error("invalid ledger entry type");
	mType = mFormat->t_type;
	if (!setType(mFormat->elements))
		throw std::runtime_error("ledger entry not valid for type");
}

SerializedLedgerEntry::SerializedLedgerEntry(LedgerEntryType type) : STObject(sfLedgerEntry), mType(type)
{
	mFormat = getLgrFormat(type);
	if (mFormat == NULL) throw std::runtime_error("invalid ledger entry type");
	set(mFormat->elements);
	setValueFieldU16(sfLedgerEntryType, static_cast<uint16>(mFormat->t_type));
}

std::string SerializedLedgerEntry::getFullText() const
{
	std::string ret = "\"";
	ret += mIndex.GetHex();
	ret += "\" = { ";
	ret += mFormat->t_name;
	ret += ", ";
	ret += STObject::getFullText();
	ret += "}";
	return ret;
}

std::string SerializedLedgerEntry::getText() const
{
	return str(boost::format("{ %s, %s }")
		% mIndex.GetHex()
		% STObject::getText());
}

Json::Value SerializedLedgerEntry::getJson(int options) const
{
	Json::Value ret(STObject::getJson(options));

	ret["index"]	= mIndex.GetHex();

	return ret;
}

bool SerializedLedgerEntry::isThreadedType()
{
	return getFieldIndex(sfLastTxnID) != -1;
}

bool SerializedLedgerEntry::isThreaded()
{
	return isFieldPresent(sfLastTxnID);
}

uint256 SerializedLedgerEntry::getThreadedTransaction()
{
	return getValueFieldH256(sfLastTxnID);
}

uint32 SerializedLedgerEntry::getThreadedLedger()
{
	return getValueFieldU32(sfLastTxnSeq);
}

bool SerializedLedgerEntry::thread(const uint256& txID, uint32 ledgerSeq, uint256& prevTxID, uint32& prevLedgerID)
{
	uint256 oldPrevTxID = getValueFieldH256(sfLastTxnID);
	Log(lsTRACE) << "Thread Tx:" << txID << " prev:" << oldPrevTxID;
	if (oldPrevTxID == txID)
		return false;
	prevTxID = oldPrevTxID;
	prevLedgerID = getValueFieldU32(sfLastTxnSeq);
	assert(prevTxID != txID);
	setValueFieldH256(sfLastTxnID, txID);
	setValueFieldU32(sfLastTxnSeq, ledgerSeq);
	return true;
}

bool SerializedLedgerEntry::hasOneOwner()
{
	return (mType != ltACCOUNT_ROOT) && (getFieldIndex(sfAccount) != -1);
}

bool SerializedLedgerEntry::hasTwoOwners()
{
	return mType == ltRIPPLE_STATE;
}

NewcoinAddress SerializedLedgerEntry::getOwner()
{
	return getValueFieldAccount(sfAccount);
}

NewcoinAddress SerializedLedgerEntry::getFirstOwner()
{
	return NewcoinAddress::createAccountID(getIValueFieldAmount(sfLowLimit).getIssuer());
}

NewcoinAddress SerializedLedgerEntry::getSecondOwner()
{
	return NewcoinAddress::createAccountID(getIValueFieldAmount(sfHighLimit).getIssuer());
}

std::vector<uint256> SerializedLedgerEntry::getOwners()
{
	std::vector<uint256> owners;
	uint160 account;

	for (int i = 0, fields = getCount(); i < fields; ++i)
	{
		SField::ref fc = getFieldSType(i);
		if ((fc == sfAccount) || (fc == sfOwner))
		{
				const STAccount* entry = dynamic_cast<const STAccount *>(peekAtPIndex(i));
				if ((entry != NULL) && entry->getValueH160(account))
					owners.push_back(Ledger::getAccountRootIndex(account));
		}
		if ((fc == sfLowLimit) || (fc == sfHighLimit))
		{
			const STAmount* entry = dynamic_cast<const STAmount *>(peekAtPIndex(i));
			if ((entry != NULL))
			{
				uint160 issuer = entry->getIssuer();
				if (issuer.isNonZero())
					owners.push_back(Ledger::getAccountRootIndex(issuer));
			}
		}
	}

	return owners;
}

// vim:ts=4
