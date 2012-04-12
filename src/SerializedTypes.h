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
	STI_HASH128=6, STI_HASH160=7, STI_HASH256=8, STI_VL=9, STI_TL=10,
	STI_AMOUNT=11,

	// high level types
	STI_ACCOUNT=100, STI_TRANSACTION=101, STI_LEDGERENTRY=102
};

class SerializedType
{
protected:
	const char *name;

public:

	SerializedType() : name(NULL) { ; }
	SerializedType(const char *n) : name(n) { ; }
	SerializedType(const SerializedType& n) : name(n.name) { ; }
	virtual ~SerializedType() { ; }

	void setName(const char *n) { name=n; }
	const char *getName() const { return name; }

	virtual int getLength() const { return 0; }
	virtual SerializedTypeID getSType() const { return STI_NOTPRESENT; }
	virtual SerializedType* duplicate() const { return new SerializedType(name); }

	virtual std::string getFullText() const;
	virtual std::string getText() const // just the value
	{ return std::string(); }

	virtual void add(Serializer& s) const { return; }

	SerializedType* new_clone(const SerializedType& s) { return s.duplicate(); }
	void delete_clone(const SerializedType* s) { boost::checked_delete(s); }

	virtual bool isEquivalent(const SerializedType& t) const { return true; }

	bool operator==(const SerializedType& t) const
	{ return (getSType()==t.getSType()) && isEquivalent(t); }
	bool operator!=(const SerializedType& t) const
	{ return (getSType()!=t.getSType()) || !isEquivalent(t); }
};

class STUInt8 : public SerializedType
{
protected:
	unsigned char value;

public:

	STUInt8(unsigned char v=0) : value(v) { ; }
	STUInt8(const char *n, unsigned char v=0) : SerializedType(n), value(v) { ; }
	static STUInt8* construct(SerializerIterator&, const char *name=NULL);

	int getLength() const { return 1; }
	SerializedTypeID getSType() const { return STI_UINT8; }
	STUInt8* duplicate() const { return new STUInt8(name, value); }
	std::string getText() const;
	void add(Serializer& s) const { s.add8(value); }

	unsigned char getValue() const { return value; }
	void setValue(unsigned char v) { value=v; }

	operator unsigned char() const { return value; }
	STUInt8& operator=(unsigned char v) { value=v; return *this; }
	virtual bool isEquivalent(const SerializedType& t) const;
};

class STUInt16 : public SerializedType
{
protected:
	uint16 value;

public:

	STUInt16(uint16 v=0) : value(v) { ; }
	STUInt16(const char *n, uint16 v=0) : SerializedType(n), value(v) { ; }
	static STUInt16* construct(SerializerIterator&, const char *name=NULL);

	int getLength() const { return 2; }
	SerializedTypeID getSType() const { return STI_UINT16; }
	STUInt16* duplicate() const { return new STUInt16(name, value); }
	std::string getText() const;
	void add(Serializer& s) const { s.add16(value); }

	uint16 getValue() const { return value; }
	void setValue(uint16 v) { value=v; }

	operator uint16() const { return value; }
	STUInt16& operator=(uint16 v) { value=v; return *this; }
	virtual bool isEquivalent(const SerializedType& t) const;
};

class STUInt32 : public SerializedType
{
protected:
	uint32 value;

public:

	STUInt32(uint32 v=0) : value(v) { ; }
	STUInt32(const char *n, uint32 v=0) : SerializedType(n), value(v) { ; }
	static STUInt32* construct(SerializerIterator&, const char *name=NULL);

	int getLength() const { return 4; }
	SerializedTypeID getSType() const { return STI_UINT32; }
	STUInt32* duplicate() const { return new STUInt32(name, value); }
	std::string getText() const;
	void add(Serializer& s) const { s.add32(value); }

	uint32 getValue() const { return value; }
	void setValue(uint32 v) { value=v; }

	operator uint32() const { return value; }
	STUInt32& operator=(uint32 v) { value=v; return *this; }
	virtual bool isEquivalent(const SerializedType& t) const;
};

class STUInt64 : public SerializedType
{
protected:
	uint64 value;

public:

	STUInt64(uint64 v=0) : value(v) { ; }
	STUInt64(const char *n, uint64 v=0) : SerializedType(n), value(v) { ; }
	static STUInt64* construct(SerializerIterator&, const char *name=NULL);

	int getLength() const { return 8; }
	SerializedTypeID getSType() const { return STI_UINT64; }
	STUInt64* duplicate() const { return new STUInt64(name, value); }
	std::string getText() const;
	void add(Serializer& s) const { s.add64(value); }

	uint64 getValue() const { return value; }
	void setValue(uint64 v) { value=v; }

	operator uint64() const { return value; }
	STUInt64& operator=(uint64 v) { value=v; return *this; }
	virtual bool isEquivalent(const SerializedType& t) const;
};

class STAmount : public SerializedType
{
	// Internal form:
	// 1: If amount is zero, then offset and value are both zero.
	// 2: Otherwise:
	//   legal offset range is -96 to +80 inclusive
	//   value range is 10^15 to (10^16 - 1) inclusive
	//  amount = value * [10 ^ offset]

	// Wire form:
	// High 8 bits are (offset+142), legal range is, 80 to 22 inclusive
	// Low 56 bits are value, legal range is 10^15 to (10^16 - 1) inclusive

protected:
	int offset; // These variables *always* hold canonical values on entry/exit
	uint64 value;

	void canonicalize();

	static const int cMinOffset=-96, cMaxOffset=80;
	static const uint64 cMinValue=1000000000000000ull, cMaxValue=9999999999999999ull;

public:
	STAmount(uint64 v=0, int off=0) : offset(off), value(v)
	{  canonicalize(); } // (1,0)=$1 (1,-2)=$.01 (100,0)=(10000,-2)=$.01
	STAmount(const char *n, uint64 v=0, int off=1) : SerializedType(n), offset(off), value(v)
	{ canonicalize(); }
	STAmount(const STAmount& a) : SerializedType(a), offset(a.offset), value(a.value) { ; }
	static STAmount* construct(SerializerIterator&, const char *name=NULL);

	int getLength() const { return 8; }
	SerializedTypeID getSType() const { return STI_AMOUNT; }
	STAmount* duplicate() const { return new STAmount(name, offset, value); }
	std::string getText() const;
	void add(Serializer& s) const;

	int getOffset() const { return offset; }
	uint64 getValue() const { return value; }
	void zero() { offset=0; value=0; }
	bool isZero() const { return value==0; }

	virtual bool isEquivalent(const SerializedType& t) const;

	bool operator==(const STAmount&) const;
	bool operator!=(const STAmount&) const;
	bool operator<(const STAmount&) const;
	bool operator>(const STAmount&) const;
	bool operator<=(const STAmount&) const;
	bool operator>=(const STAmount&) const;

	STAmount& operator+=(const STAmount&);
	STAmount& operator-=(const STAmount&);
	STAmount& operator=(const STAmount&);
	STAmount& operator+=(uint64);
	STAmount& operator-=(uint64);
	STAmount& operator=(uint64);

	operator double() const;

	friend STAmount operator+(STAmount v1, STAmount v2);
	friend STAmount operator-(STAmount v1, STAmount v2);
	friend STAmount operator/(const STAmount& v1, const STAmount& v2);
	friend STAmount operator*(const STAmount& v1, const STAmount& v2);

	// Someone is offering X for Y, what is the rate?
	friend STAmount getRate(const STAmount& offerOut, const STAmount& offerIn);

	// Someone is offering X for Y, I try to pay Z, how much do I get?
	// And what's left of the offer? And how much do I actually pay?
	friend STAmount getClaimed(STAmount& offerOut, STAmount& offerIn, STAmount& paid);

	// Someone is offering X for Y, I need Z, how much do I pay
	friend STAmount getNeeded(const STAmount& offerOut, const STAmount& offerIn, const STAmount& needed);
};

class STHash128 : public SerializedType
{
protected:
	uint128 value;

public:

	STHash128(const uint128& v=uint128()) : value(v) { ; }
	STHash128(const char *n, const uint128& v=uint128()) : SerializedType(n), value(v) { ; }
	STHash128() { ; }
	static STHash128* construct(SerializerIterator&, const char *name=NULL);

	int getLength() const { return 20; }
	SerializedTypeID getSType() const { return STI_HASH128; }
	STHash128* duplicate() const { return new STHash128(name, value); }
	virtual std::string getText() const;
	void add(Serializer& s) const { s.add128(value); }

	const uint128& getValue() const { return value; }
	void setValue(const uint128& v) { value=v; }

	operator uint128() const { return value; }
	STHash128& operator=(const uint128& v) { value=v; return *this; }
	virtual bool isEquivalent(const SerializedType& t) const;
};

class STHash160 : public SerializedType
{
protected:
	uint160 value;

public:

	STHash160(const uint160& v=uint160()) : value(v) { ; }
	STHash160(const char *n, const uint160& v=uint160()) : SerializedType(n), value(v) { ; }
	STHash160() { ; }
	static STHash160* construct(SerializerIterator&, const char *name=NULL);

	int getLength() const { return 20; }
	SerializedTypeID getSType() const { return STI_HASH160; }
	STHash160* duplicate() const { return new STHash160(name, value); }
	virtual std::string getText() const;
	void add(Serializer& s) const { s.add160(value); }

	const uint160& getValue() const { return value; }
	void setValue(const uint160& v) { value=v; }

	operator uint160() const { return value; }
	STHash160& operator=(const uint160& v) { value=v; return *this; }
	virtual bool isEquivalent(const SerializedType& t) const;
};

class STHash256 : public SerializedType
{
protected:
	uint256 value;

public:

	STHash256(const uint256& v) : value(v) { ; }
	STHash256(const char *n, const uint256& v=uint256()) : SerializedType(n), value(v) { ; }
	STHash256() { ; }
	static STHash256* construct(SerializerIterator&, const char *name=NULL);

	int getLength() const { return 32; }
	SerializedTypeID getSType() const { return STI_HASH256; }
	STHash256* duplicate() const { return new STHash256(name, value); }
	std::string getText() const;
	void add(Serializer& s) const { s.add256(value); }

	const uint256& getValue() const { return value; }
	void setValue(const uint256& v) { value=v; }

	operator uint256() const { return value; }
	STHash256& operator=(const uint256& v) { value=v; return *this; }
	virtual bool isEquivalent(const SerializedType& t) const;
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
	static STVariableLength* construct(SerializerIterator&, const char *name=NULL);

	int getLength() const;
	virtual SerializedTypeID getSType() const { return STI_VL; }
	virtual STVariableLength* duplicate() const { return new STVariableLength(name, value); }
	virtual std::string getText() const;
	void add(Serializer& s) const { s.addVL(value); }

	const std::vector<unsigned char>& peekValue() const { return value; }
	std::vector<unsigned char>& peekValue() { return value; }
	std::vector<unsigned char> getValue() const { return value; }
	void setValue(const std::vector<unsigned char>& v) { value=v; }

	operator std::vector<unsigned char>() const { return value; }
	STVariableLength& operator=(const std::vector<unsigned char>& v) { value=v; return *this; }
	virtual bool isEquivalent(const SerializedType& t) const;
};

class STAccount : public STVariableLength
{
public:

	STAccount(const std::vector<unsigned char>& v) : STVariableLength(v) { ; }
	STAccount(const char *n, const std::vector<unsigned char>& v) : STVariableLength(n, v) { ; }
	STAccount(const char *n) : STVariableLength(n) { ; }
	STAccount() { ; }
	static STAccount* construct(SerializerIterator&, const char *name=NULL);

	SerializedTypeID getSType() const { return STI_ACCOUNT; }
	virtual STAccount* duplicate() const { return new STAccount(name, value); }
	std::string getText() const;

	void setValueH160(const uint160& v);
	bool getValueH160(uint160&) const;
	bool isValueH160() const;
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
	static STTaggedList* construct(SerializerIterator&, const char *name=NULL);

	int getLength() const;
	SerializedTypeID getSType() const { return STI_TL; }
	STTaggedList* duplicate() const { return new STTaggedList(name, value); }
	std::string getText() const;
	void add(Serializer& s) const { if(s.addTaggedList(value)<0) throw(0); }

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
	virtual bool isEquivalent(const SerializedType& t) const;
};

#endif
