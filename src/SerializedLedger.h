#ifndef __SERIALIZEDLEDGER__
#define __SERIALIZEDLEDGER__

#include "SerializedObject.h"
#include "LedgerFormats.h"
#include "NewcoinAddress.h"

class SerializedLedgerEntry : public STObject
{
public:
	typedef boost::shared_ptr<SerializedLedgerEntry>		pointer;
	typedef const boost::shared_ptr<SerializedLedgerEntry>&	ref;

protected:
	uint256						mIndex;
	LedgerEntryType				mType;
	const LedgerEntryFormat*	mFormat;

	SerializedLedgerEntry* duplicate() const { return new SerializedLedgerEntry(*this); }

public:
	SerializedLedgerEntry(const Serializer& s, const uint256& index);
	SerializedLedgerEntry(SerializerIterator& sit, const uint256& index);
	SerializedLedgerEntry(LedgerEntryType type);

	SerializedTypeID getSType() const { return STI_LEDGERENTRY; }
	std::string getFullText() const;
	std::string getText() const;
	Json::Value getJson(int options) const;
	virtual bool isEquivalent(const SerializedType& t) const;

	const uint256& getIndex() const;
	void setIndex(const uint256& i);

	LedgerEntryType getType() const { return mType; }
	uint16 getVersion() const { return getValueFieldU16(sfLedgerEntryType); }
	const LedgerEntryFormat* getFormat() { return mFormat; }

	bool isThreadedType();	// is this a ledger entry that can be threaded
	bool isThreaded();		// is this ledger entry actually threaded
	bool hasOneOwner();		// This node has one other node that owns it (like nickname)
	bool hasTwoOwners();	// This node has two nodes that own it (like ripple balance)
	NewcoinAddress getOwner();
	NewcoinAddress getFirstOwner();
	NewcoinAddress getSecondOwner();
	uint256 getThreadedTransaction();
	uint32 getThreadedLedger();
	bool thread(const uint256& txID, uint32 ledgerSeq, uint256& prevTxID, uint32& prevLedgerID);
	std::vector<uint256> getOwners();	// nodes notified if this node is deleted

	// CAUTION: All these functions are now obsolete and will be removed after
	// the new serialization code is merged.
	int getIFieldIndex(SField::ref field) const { return getFieldIndex(field); }
	int getIFieldCount() const { return getCount(); }
	const SerializedType& peekIField(SField::ref field) const { return peekAtField(field); }
	SerializedType& getIField(SField::ref field) { return getField(field); }
	SField::ref getIFieldSType(int index) { return getFieldSType(index); }
	std::string getIFieldString(SField::ref field) const { return getFieldString(field); }
	unsigned char getIFieldU8(SField::ref field) const { return getValueFieldU8(field); }
	uint16 getIFieldU16(SField::ref field) const { return getValueFieldU16(field); }
	uint32 getIFieldU32(SField::ref field) const { return getValueFieldU32(field); }
	uint64 getIFieldU64(SField::ref field) const { return getValueFieldU64(field); }
	uint128 getIFieldH128(SField::ref field) const { return getValueFieldH128(field); }
	uint160 getIFieldH160(SField::ref field) const { return getValueFieldH160(field); }
	uint256 getIFieldH256(SField::ref field) const { return getValueFieldH256(field); }
	std::vector<unsigned char> getIFieldVL(SField::ref field) const { return getValueFieldVL(field); }
	std::vector<TaggedListItem> getIFieldTL(SField::ref field) const { return getValueFieldTL(field); }
	NewcoinAddress getIValueFieldAccount(SField::ref field) const { return getValueFieldAccount(field); }
	STAmount getIValueFieldAmount(SField::ref field) const { return getValueFieldAmount(field); }
	STVector256 getIFieldV256(SField::ref field) { return getValueFieldV256(field); }
	void setIFieldU8(SField::ref field, unsigned char v) { return setValueFieldU8(field, v); }
	void setIFieldU16(SField::ref field, uint16 v) { return setValueFieldU16(field, v); }
	void setIFieldU32(SField::ref field, uint32 v) { return setValueFieldU32(field, v); }
	void setIFieldU64(SField::ref field, uint64 v) { return setValueFieldU64(field, v); }
	void setIFieldH128(SField::ref field, const uint128& v) { return setValueFieldH128(field, v); }
	void setIFieldH160(SField::ref field, const uint160& v) { return setValueFieldH160(field, v); }
	void setIFieldH256(SField::ref field, const uint256& v) { return setValueFieldH256(field, v); }
	void setIFieldVL(SField::ref field, const std::vector<unsigned char>& v)
		{ return setValueFieldVL(field, v); }
	void setIFieldTL(SField::ref field, const std::vector<TaggedListItem>& v)
		{ return setValueFieldTL(field, v); }
	void setIFieldAccount(SField::ref field, const uint160& account)
		{ return setValueFieldAccount(field, account); }
	void setIFieldAccount(SField::ref field, const NewcoinAddress& account)
		{ return setValueFieldAccount(field, account); }
	void setIFieldAmount(SField::ref field, const STAmount& amount)
		{ return setValueFieldAmount(field, amount); }
	void setIFieldV256(SField::ref field, const STVector256& v) { return setValueFieldV256(field, v); }
	bool getIFieldPresent(SField::ref field) const { return isFieldPresent(field); }
	void makeIFieldPresent(SField::ref field) { makeFieldPresent(field); }
	void makeIFieldAbsent(SField::ref field) { return makeFieldAbsent(field); }
	// CAUTION: All the above functions are obsolete

};

typedef SerializedLedgerEntry SLE;

#endif
// vim:ts=4
