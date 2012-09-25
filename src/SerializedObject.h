#ifndef __SERIALIZEDOBJECT__
#define __SERIALIZEDOBJECT__

#include <vector>

#include <boost/ptr_container/ptr_vector.hpp>

#include "../json/value.h"

#include "SerializedTypes.h"

// Serializable object/array types

struct SOElement
{ // An element in the description of a serialized object
	SOE_Field e_field;
	SOE_Flags flags;
};

class STObject : public SerializedType
{
protected:
	boost::ptr_vector<SerializedType> mData;
	std::vector<const SOElement*> mType;

	STObject* duplicate() const { return new STObject(*this); }

public:
	STObject(FieldName *n = NULL) : SerializedType(n) { ; }
	STObject(const SOElement *t, FieldName *n = NULL);
	STObject(const SOElement *t, SerializerIterator& u, FieldName *n = NULL);
	virtual ~STObject() { ; }

	void set(const SOElement* t);
	void set(const SOElement* t, SerializerIterator& u, int depth = 0);

	int getLength() const;
	virtual SerializedTypeID getSType() const { return STI_OBJECT; }
	virtual bool isEquivalent(const SerializedType& t) const;

	void add(Serializer& s) const;
	Serializer getSerializer() const { Serializer s; add(s); return s; }
	std::string getFullText() const;
	std::string getText() const;
	virtual Json::Value getJson(int options) const;

	int addObject(const SerializedType& t) { mData.push_back(t.clone()); return mData.size() - 1; }
	int giveObject(std::auto_ptr<SerializedType> t) { mData.push_back(t); return mData.size() - 1; }
	int giveObject(SerializedType* t) { mData.push_back(t); return mData.size() - 1; }
	const boost::ptr_vector<SerializedType>& peekData() const { return mData; }
	boost::ptr_vector<SerializedType>& peekData() { return mData; }

	int getCount() const { return mData.size(); }

	bool setFlag(uint32);
	bool clearFlag(uint32);
	uint32 getFlags() const;

	const SerializedType& peekAtIndex(int offset) const { return mData[offset]; }
	SerializedType& getIndex(int offset) { return mData[offset]; }
	const SerializedType* peekAtPIndex(int offset) const { return &(mData[offset]); }
	SerializedType* getPIndex(int offset) { return &(mData[offset]); }

	int getFieldIndex(SOE_Field field) const;
	SOE_Field getFieldSType(int index) const;

	const SerializedType& peekAtField(SOE_Field field) const;
	SerializedType& getField(SOE_Field field);
	const SerializedType* peekAtPField(SOE_Field field) const;
	SerializedType* getPField(SOE_Field field);
	const SOElement* getFieldType(SOE_Field field) const;

	// these throw if the field type doesn't match, or return default values if the
	// field is optional but not present
	std::string getFieldString(SOE_Field field) const;
	unsigned char getValueFieldU8(SOE_Field field) const;
	uint16 getValueFieldU16(SOE_Field field) const;
	uint32 getValueFieldU32(SOE_Field field) const;
	uint64 getValueFieldU64(SOE_Field field) const;
	uint128 getValueFieldH128(SOE_Field field) const;
	uint160 getValueFieldH160(SOE_Field field) const;
	uint256 getValueFieldH256(SOE_Field field) const;
	NewcoinAddress getValueFieldAccount(SOE_Field field) const;
	std::vector<unsigned char> getValueFieldVL(SOE_Field field) const;
	std::vector<TaggedListItem> getValueFieldTL(SOE_Field field) const;
	STAmount getValueFieldAmount(SOE_Field field) const;
	STPathSet getValueFieldPathSet(SOE_Field field) const;
	STVector256 getValueFieldV256(SOE_Field field) const;

	void setValueFieldU8(SOE_Field field, unsigned char);
	void setValueFieldU16(SOE_Field field, uint16);
	void setValueFieldU32(SOE_Field field, uint32);
	void setValueFieldU64(SOE_Field field, uint64);
	void setValueFieldH128(SOE_Field field, const uint128&);
	void setValueFieldH160(SOE_Field field, const uint160&);
	void setValueFieldH256(SOE_Field field, const uint256&);
	void setValueFieldVL(SOE_Field field, const std::vector<unsigned char>&);
	void setValueFieldTL(SOE_Field field, const std::vector<TaggedListItem>&);
	void setValueFieldAccount(SOE_Field field, const uint160&);
	void setValueFieldAccount(SOE_Field field, const NewcoinAddress& addr)
	{ setValueFieldAccount(field, addr.getAccountID()); }
	void setValueFieldAmount(SOE_Field field, const STAmount&);
	void setValueFieldPathSet(SOE_Field field, const STPathSet&);
	void setValueFieldV256(SOE_Field field, const STVector256& v);

	bool isFieldPresent(SOE_Field field) const;
	SerializedType* makeFieldPresent(SOE_Field field);
	void makeFieldAbsent(SOE_Field field);

	static std::auto_ptr<SerializedType> makeDefaultObject(SerializedTypeID id, FieldName *name);
	static std::auto_ptr<SerializedType> makeDeserializedObject(SerializedTypeID id, FieldName *name,
		SerializerIterator&, int depth);

	static void unitTest();
};

class STArray : public SerializedType
{
public:
	typedef std::vector<STObject>							vector;
	typedef std::vector<STObject>::iterator					iterator;
	typedef std::vector<STObject>::const_iterator			const_iterator;
	typedef std::vector<STObject>::reverse_iterator			reverse_iterator;
	typedef std::vector<STObject>::const_reverse_iterator	const_reverse_iterator;
	typedef std::vector<STObject>::size_type				size_type;

protected:

	vector value;

	STArray* duplicate() const { return new STArray(fName, value); }
	static STArray* construct(SerializerIterator&, FieldName* name = NULL);

public:

	STArray()																{ ; }
	STArray(FieldName* f, const vector& v) : SerializedType(f), value(v)	{ ; }
	STArray(vector& v) : value(v)											{ ; }

	static std::auto_ptr<SerializedType> deserialize(SerializerIterator& sit, FieldName* name)
		{ return std::auto_ptr<SerializedType>(construct(sit, name)); }

	const vector& getValue() const			{ return value; }
	vector& getValue()						{ return value; }

	// vector-like functions
	void push_back(const STObject& object)	{ value.push_back(object); }
	STObject& operator[](int j)				{ return value[j]; }
	const STObject& operator[](int j) const	{ return value[j]; }
	iterator begin()						{ return value.begin(); }
	const_iterator begin() const			{ return value.begin(); }
	iterator end()							{ return value.end(); }
	const_iterator end() const				{ return value.end(); }
	size_type size() const					{ return value.size(); }
	reverse_iterator rbegin()				{ return value.rbegin(); }
	const_reverse_iterator rbegin() const	{ return value.rbegin(); }
	reverse_iterator rend()					{ return value.rend(); }
	const_reverse_iterator rend() const		{ return value.rend(); }
	iterator erase(iterator pos)			{ return value.erase(pos); }
	void pop_back()							{ value.pop_back(); }
	bool empty() const						{ return value.empty(); }
	void clear()							{ value.clear(); }

	virtual std::string getFullText() const;
	virtual std::string getText() const;
	virtual Json::Value getJson(int) const;
	virtual void add(Serializer& s) const;

	bool operator==(const STArray &s)		{ return value == s.value; }
	bool operator!=(const STArray &s)		{ return value != s.value; }

	virtual SerializedTypeID getSType() const { return STI_ARRAY; }
	virtual bool isEquivalent(const SerializedType& t) const;
};

#endif
// vim:ts=4
