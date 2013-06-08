#ifndef __SERIALIZEDLEDGER__
#define __SERIALIZEDLEDGER__

DEFINE_INSTANCE(SerializedLedgerEntry);

// VFALCO TODO rename this to SerializedLedger
class SerializedLedgerEntry : public STObject, private IS_INSTANCE(SerializedLedgerEntry)
{
public:
	typedef boost::shared_ptr<SerializedLedgerEntry>		pointer;
	typedef const boost::shared_ptr<SerializedLedgerEntry>&	ref;

public:
	SerializedLedgerEntry(const Serializer& s, const uint256& index);
	SerializedLedgerEntry(SerializerIterator& sit, const uint256& index);
	SerializedLedgerEntry(LedgerEntryType type, const uint256& index);

	SerializedTypeID getSType() const { return STI_LEDGERENTRY; }
	std::string getFullText() const;
	std::string getText() const;
	Json::Value getJson(int options) const;

	const uint256& getIndex() const		{ return mIndex; }
	void setIndex(const uint256& i)		{ mIndex = i; }

	void setImmutable()					{ mMutable = false; }
	bool isMutable()					{ return mMutable; }
	SerializedLedgerEntry::pointer getMutable() const;

	LedgerEntryType getType() const { return mType; }
	uint16 getVersion() const { return getFieldU16(sfLedgerEntryType); }
	const LedgerEntryFormat* getFormat() { return mFormat; }

	bool isThreadedType();	// is this a ledger entry that can be threaded
	bool isThreaded();		// is this ledger entry actually threaded
	bool hasOneOwner();		// This node has one other node that owns it (like nickname)
	bool hasTwoOwners();	// This node has two nodes that own it (like ripple balance)
	RippleAddress getOwner();
	RippleAddress getFirstOwner();
	RippleAddress getSecondOwner();
	uint256 getThreadedTransaction();
	uint32 getThreadedLedger();
	bool thread(const uint256& txID, uint32 ledgerSeq, uint256& prevTxID, uint32& prevLedgerID);
	std::vector<uint256> getOwners();	// nodes notified if this node is deleted

private:
	uint256						mIndex;
	LedgerEntryType				mType;
	const LedgerEntryFormat*	mFormat;
	bool						mMutable;

	SerializedLedgerEntry* duplicate() const { return new SerializedLedgerEntry(*this); }
};

typedef SerializedLedgerEntry SLE;

#endif
// vim:ts=4
