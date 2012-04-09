
#include "SerializedLedger.h"

SerializedLedger::SerializedLedger(SerializerIterator& sit, const uint256& index)
	: STObject("LedgerEntry"), mIndex(index)
{
	uint16 type=sit.get16();
	mFormat=getLgrFormat(static_cast<LedgerEntryType>(type));
	if(mFormat==NULL) throw std::runtime_error("invalid ledger entry type");
	mType=mFormat->t_type;
	mVersion.setValue(type);
	mObject=STObject(mFormat->elements, sit, "Entry");
}

SerializedLedger::SerializedLedger(LedgerEntryType type) : STObject("LedgerEntry"), mType(type)
{
	mFormat=getLgrFormat(type);
	if(mFormat==NULL) throw std::runtime_error("invalid ledger entry type");
	mVersion.setValue(static_cast<uint16>(mFormat->t_type));
	mObject=STObject(mFormat->elements, "Entry");
}

std::string SerializedLedger::getFullText() const
{
	std::string ret="\"";
	ret+=mIndex.GetHex();
	ret+="\" = { ";
	ret+=mFormat->t_name;
	ret+=", ";
	ret+=mObject.getFullText();
	ret+="}";
	return ret;
}

std::string SerializedLedger::getText() const
{
	std::string ret="{";
	ret+=mIndex.GetHex();
	ret+=mVersion.getText();
	ret+=mObject.getText();
	ret+="}";
	return ret;
}

bool SerializedLedger::isEquivalent(const SerializedType& t) const
{ // locators are not compared
	const SerializedLedger* v=dynamic_cast<const SerializedLedger*>(&t);
	if(!v) return false;
	if(mType != v->mType) return false;
	if(mObject != v->mObject) return false;
	return true;
}
