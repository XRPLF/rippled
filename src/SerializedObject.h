#ifndef __SERIALIZEDOBJECT__
#define __SERIALIZEDOBJECT__

#include <vector>

#include <boost/ptr_container/ptr_vector.hpp>

#include "../json/value.h"

#include "SerializedTypes.h"

// Serializable object/array types

class SOElement
{ // An element in the description of a serialized object
public:
	typedef SOElement const * ptr;			// used to point to one element

	SField::ref e_field;
	const SOE_Flags flags;

	SOElement(SField::ref fi, SOE_Flags fl) : e_field(fi), flags(fl) { ; }
};

class STObject : public SerializedType
{
protected:
	boost::ptr_vector<SerializedType> mData;
	std::vector<SOElement::ptr> mType;

	STObject* duplicate() const { return new STObject(*this); }
	STObject(SField::ref name, boost::ptr_vector<SerializedType>& data) : SerializedType(name) { mData.swap(data); }

public:
	STObject()											{ ; }

	STObject(SField::ref name) : SerializedType(name)	{ ; }

	STObject(const std::vector<SOElement::ptr>& type, SField::ref name) : SerializedType(name)
	{ set(type); }

	STObject(const std::vector<SOElement::ptr>& type, SerializerIterator& sit, SField::ref name) : SerializedType(name)
	{ set(sit); setType(type); }

	static std::auto_ptr<STObject> parseJson(const Json::Value& value, SField::ref name = sfGeneric, int depth = 0);

	virtual ~STObject() { ; }

	static std::auto_ptr<SerializedType> deserialize(SerializerIterator& sit, SField::ref name);

	bool setType(const std::vector<SOElement::ptr>& type);
	bool isValidForType();
	bool isFieldAllowed(SField::ref);
	bool isFree() const { return mType.empty(); }

	void set(const std::vector<SOElement::ptr>&);
	bool set(SerializerIterator& u, int depth = 0);

	virtual SerializedTypeID getSType() const { return STI_OBJECT; }
	virtual bool isEquivalent(const SerializedType& t) const;

	virtual void add(Serializer& s) const	{ add(s, true);	} // just inner elements
	void add(Serializer& s, bool withSignature) const;
	Serializer getSerializer() const { Serializer s; add(s); return s; }
	std::string getFullText() const;
	std::string getText() const;
	virtual Json::Value getJson(int options) const;

	int addObject(const SerializedType& t)			{ mData.push_back(t.clone()); return mData.size() - 1; }
	int giveObject(std::auto_ptr<SerializedType> t)	{ mData.push_back(t); return mData.size() - 1; }
	int giveObject(SerializedType* t)				{ mData.push_back(t); return mData.size() - 1; }
	const boost::ptr_vector<SerializedType>& peekData() const { return mData; }
	boost::ptr_vector<SerializedType>& peekData() 	{ return mData; }
	SerializedType& front() 						{ return mData.front(); }
	const SerializedType& front() const				{ return mData.front(); }
	SerializedType& back()							{ return mData.back(); }
	const SerializedType& back() const				{ return mData.back(); }

	int getCount() const { return mData.size(); }

	bool setFlag(uint32);
	bool clearFlag(uint32);
	uint32 getFlags() const;

	uint256 getHash(uint32 prefix) const;
	uint256 getSigningHash(uint32 prefix) const;

	const SerializedType& peekAtIndex(int offset) const { return mData[offset]; }
	SerializedType& getIndex(int offset) { return mData[offset]; }
	const SerializedType* peekAtPIndex(int offset) const { return &(mData[offset]); }
	SerializedType* getPIndex(int offset) { return &(mData[offset]); }

	int getFieldIndex(SField::ref field) const;
	SField::ref getFieldSType(int index) const;

	const SerializedType& peekAtField(SField::ref field) const;
	SerializedType& getField(SField::ref field);
	const SerializedType* peekAtPField(SField::ref field) const;
	SerializedType* getPField(SField::ref field, bool createOkay = false);

	// these throw if the field type doesn't match, or return default values if the
	// field is optional but not present
	std::string getFieldString(SField::ref field) const;
	unsigned char getFieldU8(SField::ref field) const;
	uint16 getFieldU16(SField::ref field) const;
	uint32 getFieldU32(SField::ref field) const;
	uint64 getFieldU64(SField::ref field) const;
	uint128 getFieldH128(SField::ref field) const;
	uint160 getFieldH160(SField::ref field) const;
	uint256 getFieldH256(SField::ref field) const;
	NewcoinAddress getFieldAccount(SField::ref field) const;
	uint160 getFieldAccount160(SField::ref field) const;
	std::vector<unsigned char> getFieldVL(SField::ref field) const;
	std::vector<TaggedListItem> getFieldTL(SField::ref field) const;
	STAmount getFieldAmount(SField::ref field) const;
	STPathSet getFieldPathSet(SField::ref field) const;
	STVector256 getFieldV256(SField::ref field) const;

	void setFieldU8(SField::ref field, unsigned char);
	void setFieldU16(SField::ref field, uint16);
	void setFieldU32(SField::ref field, uint32);
	void setFieldU64(SField::ref field, uint64);
	void setFieldH128(SField::ref field, const uint128&);
	void setFieldH160(SField::ref field, const uint160&);
	void setFieldH256(SField::ref field, const uint256&);
	void setFieldVL(SField::ref field, const std::vector<unsigned char>&);
	void setFieldTL(SField::ref field, const std::vector<TaggedListItem>&);
	void setFieldAccount(SField::ref field, const uint160&);
	void setFieldAccount(SField::ref field, const NewcoinAddress& addr)
	{ setFieldAccount(field, addr.getAccountID()); }
	void setFieldAmount(SField::ref field, const STAmount&);
	void setFieldPathSet(SField::ref field, const STPathSet&);
	void setFieldV256(SField::ref field, const STVector256& v);

	STObject& peekFieldObject(SField::ref field);

	bool isFieldPresent(SField::ref field) const;
	SerializedType* makeFieldPresent(SField::ref field);
	void makeFieldAbsent(SField::ref field);
	bool delField(SField::ref field);
	void delField(int index);

	static std::auto_ptr<SerializedType> makeDefaultObject(SerializedTypeID id, SField::ref name);
	static std::auto_ptr<SerializedType> makeDeserializedObject(SerializedTypeID id, SField::ref name,
		SerializerIterator&, int depth);

	static std::auto_ptr<SerializedType> makeNonPresentObject(SField::ref name)
	{ return makeDefaultObject(STI_NOTPRESENT, name); }
	static std::auto_ptr<SerializedType> makeDefaultObject(SField::ref name)
	{ return makeDefaultObject(name.fieldType, name); }

	bool operator==(const STObject& o) const;
	bool operator!=(const STObject& o) const { return ! (*this == o); }
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
	STArray(int n)															{ value.reserve(n); }
	STArray(SField::ref f) : SerializedType(f)								{ ; }
	STArray(SField::ref f, int n) : SerializedType(f)						{ value.reserve(n); }
	STArray(SField::ref f, const vector& v) : SerializedType(f), value(v)	{ ; }
	STArray(vector& v) : value(v)											{ ; }

	static std::auto_ptr<SerializedType> deserialize(SerializerIterator& sit, SField::ref name)
		{ return std::auto_ptr<SerializedType>(construct(sit, name)); }

	const vector& getValue() const					{ return value; }
	vector& getValue()								{ return value; }

	// vector-like functions
	void push_back(const STObject& object)			{ value.push_back(object); }
	STObject& operator[](int j)						{ return value[j]; }
	const STObject& operator[](int j) const			{ return value[j]; }
	iterator begin()								{ return value.begin(); }
	const_iterator begin() const					{ return value.begin(); }
	iterator end()									{ return value.end(); }
	const_iterator end() const						{ return value.end(); }
	size_type size() const							{ return value.size(); }
	reverse_iterator rbegin()						{ return value.rbegin(); }
	const_reverse_iterator rbegin() const			{ return value.rbegin(); }
	reverse_iterator rend()							{ return value.rend(); }
	const_reverse_iterator rend() const				{ return value.rend(); }
	iterator erase(iterator pos)					{ return value.erase(pos); }
	STObject& front()								{ return value.front(); }
	const STObject& front() const					{ return value.front(); }
	STObject& back()								{ return value.back(); }
	const STObject& back() const					{ return value.back(); }
	void pop_back()									{ value.pop_back(); }
	bool empty() const								{ return value.empty(); }
	void clear()									{ value.clear(); }
	void swap(STArray& a)							{ value.swap(a.value); }

	virtual std::string getFullText() const;
	virtual std::string getText() const;
	virtual Json::Value getJson(int) const;
	virtual void add(Serializer& s) const;

	void sort(bool (*compare)(const STObject& o1, const STObject& o2));

	bool operator==(const STArray &s)				{ return value == s.value; }
	bool operator!=(const STArray &s)				{ return value != s.value; }

	virtual SerializedTypeID getSType() const 		{ return STI_ARRAY; }
	virtual bool isEquivalent(const SerializedType& t) const;
};

#endif
// vim:ts=4
