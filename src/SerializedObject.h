#ifndef __SERIALIZEDOBJECT__
#define __SERIALIZEDOBJECT__

#include <vector>

#include <boost/ptr_container/ptr_vector.hpp>

#include "SerializedTypes.h"

enum SOE_Type
{
	SOE_NEVER=-1, SOE_REQUIRED=0, SOE_FLAGS, SOE_IFFLAG=1, SOE_IFNFLAG=2
};

enum SOE_Field
{
	sfInvalid=-1,
	sfGeneric=0,

	// common fields
	sfFlags, sfSequence, sfExtensions, sfTargetLedger, sfSourceTag, sfIdentifier,
	sfDestination, sfTarget, sfAmount, sfCurrency,
	sfAmountIn, sfAmountOut, sfCurrencyIn, sfCurrencyOut,
	sfInvoiceID,
	sfExpireLedger
};

struct SOElement
{ // An element in the description of a serialized object
	SOE_Field e_field;
	const char *e_name;
	SerializedTypeID e_id;
	SOE_Type e_type;
	int e_flags;
};

class STObject : public SerializedType
{
protected:
	int mFlagIdx; // the offset to the flags object, -1 if none
	boost::ptr_vector<SerializedType> mData;
	std::vector<SOElement*> mType;

	static SerializedType* makeDefaultObject(SerializedTypeID id, const char *name);
	static SerializedType* makeDeserializedObject(SerializedTypeID id, const char *name, SerializerIterator&);

public:
	STObject(const char *n=NULL) : SerializedType(n), mFlagIdx(-1) { ; }
	STObject(SOElement *t, const char *n=NULL);
	STObject(SOElement *t, SerializerIterator& u, const char *n=NULL);
	virtual ~STObject() { ; }

	int getLength() const;
	SerializedTypeID getType() const { return STI_OBJECT; }
	STObject* duplicate() const { return new STObject(*this); }

	void add(Serializer& s) const;
	std::string getFullText() const;
	std::string getText() const;

	int addObject(const SerializedType& t) { mData.push_back(t.duplicate()); return mData.size()-1; }
	int giveObject(SerializedType* t) { mData.push_back(t); return mData.size()-1; }
	const boost::ptr_vector<SerializedType>& peekData() const { return mData; }
	boost::ptr_vector<SerializedType>& peekData() { return mData; }

	int getCount() const { return mData.size(); }

	bool setFlag(int);
	bool clearFlag(int);
	int getFlag() const;

	const SerializedType& peekAtIndex(int offset) const { return mData[offset]; }
	SerializedType& getIndex(int offset) { return mData[offset]; }
	const SerializedType* peekAtPIndex(int offset) const { return &(mData[offset]); }
	SerializedType* getPIndex(int offset) { return &(mData[offset]); }

	int getFieldIndex(SOE_Field field) const;

	const SerializedType& peekAtField(SOE_Field field) const;
	SerializedType& getField(SOE_Field field);
	const SerializedType* peekAtPField(SOE_Field field);
	SerializedType* getPField(SOE_Field field);
	const SOElement* getFieldType(SOE_Field field) const;

	bool isFieldPresent(SOE_Field field) const;
	void makeFieldPresent(SOE_Field field);
	void makeFieldAbsent(SOE_Field field);
};


#endif
