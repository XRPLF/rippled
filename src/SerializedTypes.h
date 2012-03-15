#ifndef __SERIALIZEDTYPES__
#define __SERIALIZEDTYPES__

#include <vector>
#include <string>

#include "uint256.h"
#include "Serializer.h"

enum SerializedTypeID
{
	STI_NOTPRESENT=0, STI_OBJECT=1,
	STI_UINT8=2, STI_UINT16=3, STI_UINT32=4, STI_UINT64=5,
	STI_HASH160=6, STI_HASH256=7, STI_VL=8, STI_TL=8
};

class SerializedType
{
public:

	SerializedType() { ; }
	virtual ~SerializedType() { ; }

	virtual int getLength() const { return 0; }
	virtual SerializedTypeID getType() const { return STI_NOTPRESENT; }
	virtual SerializedType* duplicate() const { return new SerializedType(); }

	virtual std::string getText() const { return std::string(); }

	virtual void add(Serializer& s) const { return; }

	SerializedType* new_clone(const SerializedType& s) { return s.duplicate(); }
	void delete_clone(const SerializedType* s) { boost::checked_delete(s); }
};

class STUInt8 : public SerializedType
{
protected:
	unsigned char value;

public:

	STUInt8(unsigned char v=0) : value(v) { ; }
	static STUInt8* construct(SerializerIterator&);

	int getLength() const { return 1; }
	SerializedTypeID getType() const { return STI_UINT8; }
	STUInt8 *duplicate() const { return new STUInt8(value); }
	std::string getText() const;
	virtual void add(Serializer& s) const { s.add8(value); }

	unsigned char getValue() const { return value; }
	void setValue(unsigned char v) { value=v; }

	operator unsigned char() const { return value; }
	STUInt8& operator=(unsigned char v) { value=v; return *this; }
};

class STUInt16 : public SerializedType
{
protected:
	uint16 value;

public:

	STUInt16(uint16 v=0) : value(v) { ; }
	static STUInt16* construct(SerializerIterator&);

	int getLength() const { return 2; }
	SerializedTypeID getType() const { return STI_UINT16; }
	STUInt16 *duplicate() const { return new STUInt16(value); }
	std::string getText() const;
	virtual void add(Serializer& s) const { s.add16(value); }

	uint16 getValue() const { return value; }
	void setValue(uint16 v) { value=v; }

	operator uint16() const { return value; }
	STUInt16& operator=(uint16 v) { value=v; return *this; }
};

class STUInt32 : public SerializedType
{
protected:
	uint32 value;

public:

	STUInt32(uint32 v=0) : value(v) { ; }
	static STUInt32* construct(SerializerIterator&);

	int getLength() const { return 4; }
	SerializedTypeID getType() const { return STI_UINT32; }
	STUInt32 *duplicate() const { return new STUInt32(value); }
	std::string getText() const;
	virtual void add(Serializer& s) const { s.add32(value); }

	uint32 getValue() const { return value; }
	void setValue(uint32 v) { value=v; }

	operator uint32() const { return value; }
	STUInt32& operator=(uint32 v) { value=v; return *this; }
};

class STUInt64 : public SerializedType
{
protected:
	uint64 value;

public:

	STUInt64(uint64 v=0) : value(v) { ; }
	static STUInt64* construct(SerializerIterator&);

	int getLength() const { return 8; }
	SerializedTypeID getType() const { return STI_UINT64; }
	STUInt64 *duplicate() const { return new STUInt64(value); }
	std::string getText() const;
	virtual void add(Serializer& s) const { s.add64(value); }

	uint64 getValue() const { return value; }
	void setValue(uint64 v) { value=v; }

	operator uint64() const { return value; }
	STUInt64& operator=(uint64 v) { value=v; return *this; }
};

class STUHash160 : public SerializedType
{
protected:
	uint160 value;

public:

	STUHash160(const uint160& v) : value(v) { ; }
	STUHash160() { ; }
	static STUHash160* construct(SerializerIterator&);

	int getLength() const { return 20; }
	SerializedTypeID getType() const { return STI_HASH160; }
	STUHash160 *duplicate() const { return new STUHash160(value); }
	std::string getText() const;
	virtual void add(Serializer& s) const { s.add160(value); }

	const uint160& getValue() const { return value; }
	void setValue(const uint160& v) { value=v; }

	operator uint160() const { return value; }
	STUHash160& operator=(const uint160& v) { value=v; return *this; }
};

class STUHash256 : public SerializedType
{
protected:
	uint256 value;

public:

	STUHash256(const uint256& v) : value(v) { ; }
	STUHash256() { ; }
	static STUHash256* construct(SerializerIterator&);

	int getLength() const { return 32; }
	SerializedTypeID getType() const { return STI_HASH256; }
	STUHash256 *duplicate() const { return new STUHash256(value); }
	std::string getText() const;
	virtual void add(Serializer& s) const { s.add256(value); }

	const uint256& getValue() const { return value; }
	void setValue(const uint256& v) { value=v; }

	operator uint256() const { return value; }
	STUHash256& operator=(const uint256& v) { value=v; return *this; }
};

class STUVariableLength
{ // variable length byte string protected:
protected:
	std::vector<unsigned char> value;

public:

	STUVariableLength(const std::vector<unsigned char>& v) : value(v) { ; }
	STUVariableLength() { ; }
	static STUVariableLength* construct(SerializerIterator&);

	int getLength() const;
	SerializedTypeID getType() const { return STI_VL; }
	STUVariableLength *duplicate() const { return new STUVariableLength(value); }
	std::string getText() const;
	virtual void add(Serializer& s) const { s.addVL(value); }

	const std::vector<unsigned char>& peekValue() const { return value; }
	std::vector<unsigned char>& peekValue() { return value; }
	std::vector<unsigned char> getValue() const { return value; }
	void setValue(std::vector<unsigned char>& v) { value=v; }

	operator std::vector<unsigned char>() const { return value; }
	STUVariableLength& operator=(const std::vector<unsigned char>& v) { value=v; return *this; }
};

class STUTaggedList
{
protected:
	std::list<TaggedListItem> value;

public:

	STUTaggedList(const std::list<TaggedListItem>& v) : value(v) { ; }
	STUTaggedList() { ; }
	static STUTaggedList* construct(SerializerIterator&);

	int getLength() const;
	SerializedTypeID getType() const { return STI_TL; }
	STUTaggedList *duplicate() const { return new STUTaggedList(value); }
	std::string getText() const;
	virtual void add(Serializer& s) const { s.addTaggedList(value); }

	const std::list<TaggedListItem>& peekValue() const { return value; }
	std::list<TaggedListItem>& peekValue() { return value; }
	std::list<TaggedListItem> getValue() const { return value; }

	void setValue(std::list<TaggedListItem>& v) { value=v; }

	int getItemCount() const { return value.size(); }
	bool isEmpty() const { return value.empty(); }

	void clear() { value.erase(value.begin(), value.end()); }
	void addItem(const TaggedListItem& v) { value.push_back(v); }

	operator std::list<TaggedListItem>() const { return value; }
	STUTaggedList& operator=(const std::list<TaggedListItem>& v) { value=v; return *this; }
};

#endif
