#ifndef __SERIALIZEDOBJECT__
#define __SERIALIZEDOBJECT__

#include <vector>

#include <boost/ptr_container/ptr_vector.hpp>

#include "../json/value.h"

#include "SerializedTypes.h"

// Serializable object/array types

struct SOElement
{ // An element in the description of a serialized object
	SField::ref e_field;
	SOE_Flags flags;
};

class STObject : public SerializedType
{
protected:
	boost::ptr_vector<SerializedType> mData;
	std::vector<const SOElement*> mType;

	STObject* duplicate() const { return new STObject(*this); }
	static STObject* construct(SerializerIterator&, SField::ref);

public:
	STObject(SField *n = NULL) : SerializedType(n) { ; }
	STObject(const SOElement *t, SField *n = NULL);
	STObject(const SOElement *t, SerializerIterator& u, SField *n = NULL);
	virtual ~STObject() { ; }

	static std::auto_ptr<SerializedType> deserialize(SerializerIterator& sit, SField::ref name)
		{ return std::auto_ptr<SerializedType>(construct(sit, name)); }

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

	int getFieldIndex(SField::ref field) const;
	SField::ref getFieldSType(int index) const;

	const SerializedType& peekAtField(SField::ref field) const;
	SerializedType& getField(SField::ref field);
	const SerializedType* peekAtPField(SField::ref field) const;
	SerializedType* getPField(SField::ref field);
	const SOElement* getFieldType(SField::ref field) const;

	// these throw if the field type doesn't match, or return default values if the
	// field is optional but not present
	std::string getFieldString(SField::ref field) const;
	unsigned char getValueFieldU8(SField::ref field) const;
	uint16 getValueFieldU16(SField::ref field) const;
	uint32 getValueFieldU32(SField::ref field) const;
	uint64 getValueFieldU64(SField::ref field) const;
	uint128 getValueFieldH128(SField::ref field) const;
	uint160 getValueFieldH160(SField::ref field) const;
	uint256 getValueFieldH256(SField::ref field) const;
	NewcoinAddress getValueFieldAccount(SField::ref field) const;
	std::vector<unsigned char> getValueFieldVL(SField::ref field) const;
	std::vector<TaggedListItem> getValueFieldTL(SField::ref field) const;
	STAmount getValueFieldAmount(SField::ref field) const;
	STPathSet getValueFieldPathSet(SField::ref field) const;
	STVector256 getValueFieldV256(SField::ref field) const;

	void setValueFieldU8(SField::ref field, unsigned char);
	void setValueFieldU16(SField::ref field, uint16);
	void setValueFieldU32(SField::ref field, uint32);
	void setValueFieldU64(SField::ref field, uint64);
	void setValueFieldH128(SField::ref field, const uint128&);
	void setValueFieldH160(SField::ref field, const uint160&);
	void setValueFieldH256(SField::ref field, const uint256&);
	void setValueFieldVL(SField::ref field, const std::vector<unsigned char>&);
	void setValueFieldTL(SField::ref field, const std::vector<TaggedListItem>&);
	void setValueFieldAccount(SField::ref field, const uint160&);
	void setValueFieldAccount(SField::ref field, const NewcoinAddress& addr)
	{ setValueFieldAccount(field, addr.getAccountID()); }
	void setValueFieldAmount(SField::ref field, const STAmount&);
	void setValueFieldPathSet(SField::ref field, const STPathSet&);
	void setValueFieldV256(SField::ref field, const STVector256& v);

	bool isFieldPresent(SField::ref field) const;
	SerializedType* makeFieldPresent(SField::ref field);
	void makeFieldAbsent(SField::ref field);

	static std::auto_ptr<SerializedType> makeDefaultObject(SerializedTypeID id, SField *name);
	static std::auto_ptr<SerializedType> makeDeserializedObject(SerializedTypeID id, SField *name,
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

	STArray* duplicate() const { return new STArray(*this); }
	static STArray* construct(SerializerIterator&, SField::ref);

public:

	STArray()																{ ; }
	STArray(SField::ref f, const vector& v) : SerializedType(f), value(v)	{ ; }
	STArray(vector& v) : value(v)											{ ; }

	static std::auto_ptr<SerializedType> deserialize(SerializerIterator& sit, SField::ref name)
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
