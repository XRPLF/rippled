#include "SerializedLedger.h"

#include <boost/format.hpp>

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
// vim:ts=4
