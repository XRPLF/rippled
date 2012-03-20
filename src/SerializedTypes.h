#ifndef __SERIALIZEDTYPES__
#define __SERIALIZEDTYPES__

#include <vector>
#include <string>

#include "uint256.h"
#include "Serializer.h"

enum SerializedTypeID
{
	// special types
	STI_DONE=-1, STI_NOTPRESENT=0,

	// standard types
	STI_OBJECT=1, STI_UINT8=2, STI_UINT16=3, STI_UINT32=4, STI_UINT64=5,
	STI_HASH160=6, STI_HASH256=7, STI_VL=8, STI_TL=9,

	// high level types
	STI_TRANSACTION=10
};

class SerializedType
{
protected:
	const char *name;

public:

	SerializedType() : name(NULL) { ; }
	SerializedType(const char *n) : name(n) { ; }
	virtual ~SerializedType() { ; }

	void setName(const char *n) { name=n; }
	const char *getName() { return name; }

	virtual int getLength() const { return 0; }
	virtual SerializedTypeID getType() const { return STI_NOTPRESENT; }
	virtual SerializedType* duplicate() const { return new SerializedType(); }

	virtual std::string getFullText() const;
	virtual std::string getText() const // just the value
	{ return std::string(); }

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
	STUInt8(const char *n, unsigned char v=0) : SerializedType(n), value(v) { ; }
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
	STUInt16(const char *n, uint16 v=0) : SerializedType(n), value(v) { ; }
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
	STUInt32(const char *n, uint32 v=0) : SerializedType(n), value(v) { ; }
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
	STUInt64(const char *n, uint64 v=0) : SerializedType(n), value(v) { ; }
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

	STUHash160(const uint160& v=uint160()) : value(v) { ; }
	STUHash160(const char *n, const uint160& v=uint160()) : SerializedType(n), value(v) { ; }
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
	STUHash256(const char *n, const uint256& v=uint256()) : SerializedType(n), value(v) { ; }
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

class STVariableLength : public SerializedType
{ // variable length byte string protected:
protected:
	std::vector<unsigned char> value;

public:

	STVariableLength(const std::vector<unsigned char>& v) : value(v) { ; }
	STVariableLength(const char *n, const std::vector<unsigned char>& v) : SerializedType(n), value(v) { ; }
	STVariableLength(const char *n) : SerializedType(n) { ; }
	STVariableLength() { ; }
	static STVariableLength* construct(SerializerIterator&);

	int getLength() const;
	SerializedTypeID getType() const { return STI_VL; }
	STVariableLength *duplicate() const { return new STVariableLength(value); }
	std::string getText() const;
	virtual void add(Serializer& s) const { s.addVL(value); }

	const std::vector<unsigned char>& peekValue() const { return value; }
	std::vector<unsigned char>& peekValue() { return value; }
	std::vector<unsigned char> getValue() const { return value; }
	void setValue(const std::vector<unsigned char>& v) { value=v; }

	operator std::vector<unsigned char>() const { return value; }
	STVariableLength& operator=(const std::vector<unsigned char>& v) { value=v; return *this; }
};

class STTaggedList : public SerializedType
{
protected:
	std::vector<TaggedListItem> value;

public:

	STTaggedList() { ; }
	STTaggedList(const char *n) : SerializedType(n) { ; }
	STTaggedList(const std::vector<TaggedListItem>& v) : value(v) { ; }
	STTaggedList(const char *n, const std::vector<TaggedListItem>& v) : SerializedType(n), value(v) { ; }
	static STTaggedList* construct(SerializerIterator&);

	int getLength() const;
	SerializedTypeID getType() const { return STI_TL; }
	STTaggedList *duplicate() const { return new STTaggedList(value); }
	std::string getText() const;
	virtual void add(Serializer& s) const { if(s.addTaggedList(value)<0) throw(0); }

	const std::vector<TaggedListItem>& peekValue() const { return value; }
	std::vector<TaggedListItem>& peekValue() { return value; }
	std::vector<TaggedListItem> getValue() const { return value; }

	void setValue(const std::vector<TaggedListItem>& v) { value=v; }

	int getItemCount() const { return value.size(); }
	bool isEmpty() const { return value.empty(); }

	void clear() { value.erase(value.begin(), value.end()); }
	void addItem(const TaggedListItem& v) { value.push_back(v); }

	operator std::vector<TaggedListItem>() const { return value; }
	STTaggedList& operator=(const std::vector<TaggedListItem>& v) { value=v; return *this; }
};

#endif
