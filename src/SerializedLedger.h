#ifndef __SERIALIZEDLEDGER__
#define __SERIALIZEDLEDGER__

#include "SerializedObject.h"
#include "LedgerFormats.h"

class SerializedLedger : public STObject
{
public:
	typedef boost::shared_ptr<SerializedLedger> pointer;

protected:
	LedgerEntryType mType;
	STUInt16 mVersion;
	STObject mObject;
	LedgerEntryFormat* mFormat;

public:
	SerializedLedger(SerializerIterator& sit);
	SerializedLedger(LedgerEntryType type);

	int getLength() const { return mVersion.getLength() + mObject.getLength(); }
	SerializedTypeID getType() const { return STI_LEDGERENTRY; }
    SerializedLedger* duplicate() const { return new SerializedLedger(*this); }
    std::string getFullText() const;
    std::string getText() const;
    void add(Serializer& s) const { mVersion.add(s); mObject.add(s); }


};

#endif
