#ifndef __SERIALIZEDTYPES__
#define __SERIALIZEDTYPES__

#include <vector>
#include <string>

#include "../json/value.h"

#include "uint256.h"
#include "Serializer.h"

enum SerializedTypeID
{
	// special types
	STI_DONE=-1, STI_NOTPRESENT=0,

	// standard types
	STI_OBJECT=1, STI_UINT8=2, STI_UINT16=3, STI_UINT32=4, STI_UINT64=5,
	STI_HASH128=6, STI_HASH160=7, STI_HASH256=8, STI_VL=9, STI_TL=10,
	STI_AMOUNT=11, STI_PATHSET=12,

	// high level types
	STI_ACCOUNT=100, STI_TRANSACTION=101, STI_LEDGERENTRY=102
};

class SerializedType
{
protected:
	const char* name;

	virtual SerializedType* duplicate() const { return new SerializedType(name); }

public:

	SerializedType() : name(NULL) { ; }
	SerializedType(const char* n) : name(n) { ; }
	SerializedType(const SerializedType& n) : name(n.name) { ; }
	virtual ~SerializedType() { ; }

	static std::auto_ptr<SerializedType> deserialize(const char* name)
	{ return std::auto_ptr<SerializedType>(new SerializedType(name)); }

	void setName(const char* n) { name=n; }
	const char *getName() const { return name; }

	virtual int getLength() const { return 0; }
	virtual SerializedTypeID getSType() const { return STI_NOTPRESENT; }
	std::auto_ptr<SerializedType> clone() const { return std::auto_ptr<SerializedType>(duplicate()); }

	virtual std::string getFullText() const;
	virtual std::string getText() const // just the value
	{ return std::string(); }
	virtual Json::Value getJson(int) const
	{ return getText(); }

	virtual void add(Serializer& s) const { return; }

	virtual bool isEquivalent(const SerializedType& t) const { return true; }

	bool operator==(const SerializedType& t) const
	{ return (getSType() == t.getSType()) && isEquivalent(t); }
	bool operator!=(const SerializedType& t) const
	{ return (getSType() != t.getSType()) || !isEquivalent(t); }
};

inline SerializedType* new_clone(const SerializedType& s) { return s.clone().release(); }
inline void delete_clone(const SerializedType* s) { boost::checked_delete(s); }

class STUInt8 : public SerializedType
{
protected:
	unsigned char value;

	STUInt8* duplicate() const { return new STUInt8(name, value); }
	static STUInt8* construct(SerializerIterator&, const char* name = NULL);

public:

	STUInt8(unsigned char v=0) : value(v) { ; }
	STUInt8(const char* n, unsigned char v=0) : SerializedType(n), value(v) { ; }
	static std::auto_ptr<SerializedType> deserialize(SerializerIterator& sit, const char* name)
	{ return std::auto_ptr<SerializedType>(construct(sit, name)); }

	int getLength() const { return 1; }
	SerializedTypeID getSType() const { return STI_UINT8; }
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

	STUInt16* duplicate() const { return new STUInt16(name, value); }
	static STUInt16* construct(SerializerIterator&, const char* name = NULL);

public:

	STUInt16(uint16 v=0) : value(v) { ; }
	STUInt16(const char* n, uint16 v=0) : SerializedType(n), value(v) { ; }
	static std::auto_ptr<SerializedType> deserialize(SerializerIterator& sit, const char* name)
	{ return std::auto_ptr<SerializedType>(construct(sit, name)); }

	int getLength() const { return 2; }
	SerializedTypeID getSType() const { return STI_UINT16; }
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

	STUInt32* duplicate() const { return new STUInt32(name, value); }
	static STUInt32* construct(SerializerIterator&, const char* name = NULL);

public:

	STUInt32(uint32 v=0) : value(v) { ; }
	STUInt32(const char* n, uint32 v=0) : SerializedType(n), value(v) { ; }
	static std::auto_ptr<SerializedType> deserialize(SerializerIterator& sit, const char* name)
	{ return std::auto_ptr<SerializedType>(construct(sit, name)); }

	int getLength() const { return 4; }
	SerializedTypeID getSType() const { return STI_UINT32; }
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

	STUInt64* duplicate() const { return new STUInt64(name, value); }
	static STUInt64* construct(SerializerIterator&, const char* name = NULL);

public:

	STUInt64(uint64 v=0) : value(v) { ; }
	STUInt64(const char* n, uint64 v=0) : SerializedType(n), value(v) { ; }
	static std::auto_ptr<SerializedType> deserialize(SerializerIterator& sit, const char* name)
	{ return std::auto_ptr<SerializedType>(construct(sit, name)); }

	int getLength() const { return 8; }
	SerializedTypeID getSType() const { return STI_UINT64; }
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
	// 1: If amount is zero, then value is zero and offset is -100
	// 2: Otherwise:
	//   legal offset range is -96 to +80 inclusive
	//   value range is 10^15 to (10^16 - 1) inclusive
	//  amount = value * [10 ^ offset]

	// Wire form:
	// High 8 bits are (offset+142), legal range is, 80 to 22 inclusive
	// Low 56 bits are value, legal range is 10^15 to (10^16 - 1) inclusive

protected:
	uint160 mCurrency;
	uint64 mValue;
	int mOffset;
	bool mIsNative;

	void canonicalize();
	STAmount* duplicate() const { return new STAmount(name, mCurrency, mValue, mOffset); }
	static STAmount* construct(SerializerIterator&, const char* name = NULL);

	static const int cMinOffset = -96, cMaxOffset = 80;
	static const uint64 cMinValue = 1000000000000000ull, cMaxValue = 9999999999999999ull;
	static const uint64 cMaxNative = 9000000000000000000ull;
	static const uint64 cNotNative = 0x8000000000000000ull;

public:
	STAmount(uint64 v = 0) : mValue(v), mOffset(0), mIsNative(true)
	{ ; }

	STAmount(const char* n, uint64 v = 0) : SerializedType(n), mValue(v), mOffset(0), mIsNative(true)
	{ ; }

	STAmount(const uint160& currency, uint64 v = 0, int off = 0) : mCurrency(currency), mValue(v), mOffset(off)
	{ canonicalize(); }

	STAmount(const char* n, const uint160& currency, uint64 v = 0, int off = 0) : SerializedType(n),
		mCurrency(currency), mValue(v), mOffset(off)
	{ canonicalize(); }

	static std::auto_ptr<SerializedType> deserialize(SerializerIterator& sit, const char* name)
	{ return std::auto_ptr<SerializedType>(construct(sit, name)); }

	int getLength() const { return mIsNative ? 8 : 28; }
	SerializedTypeID getSType() const { return STI_AMOUNT; }
	std::string getText() const;
	std::string getRaw() const;
	void add(Serializer& s) const;

	int getOffset() const { return mOffset; }
	uint64 getValue() const { return mValue; }
	void setValue(const STAmount& v) { mValue=v; }
	std::string getCurrencyHuman();
	bool isNative() const { return mIsNative; }
	const uint160& getCurrency() const { return mCurrency; }
	void zero() { mOffset = mIsNative ? -100 : 0; mValue = 0; }
	bool isZero() const { return mValue == 0; }
	bool setValue(const std::string& sAmount, const std::string& sCurrency);

	virtual bool isEquivalent(const SerializedType& t) const;

	bool operator==(const STAmount&) const;
	bool operator!=(const STAmount&) const;
	bool operator<(const STAmount&) const;
	bool operator>(const STAmount&) const;
	bool operator<=(const STAmount&) const;
	bool operator>=(const STAmount&) const;
	bool isComparable(const STAmount&) const;
	void throwComparable(const STAmount&) const;

	STAmount& operator+=(const STAmount&);
	STAmount& operator-=(const STAmount&);
	STAmount& operator=(const STAmount&);
	STAmount& operator+=(uint64);
	STAmount& operator-=(uint64);
	STAmount& operator=(uint64);

	operator double() const;

	friend STAmount operator+(STAmount v1, STAmount v2);
	friend STAmount operator-(STAmount v1, STAmount v2);

	friend STAmount divide(const STAmount& v1, const STAmount& v2, const uint160& currencyOut);
	friend STAmount multiply(const STAmount& v1, const STAmount& v2, const uint160& currencyOut);

	// Someone is offering X for Y, what is the rate?
	friend uint64 getRate(const STAmount& offerOut, const STAmount& offerIn);

	// Someone is offering X for Y, I try to pay Z, how much do I get?
	// And what's left of the offer? And how much do I actually pay?
	friend STAmount getClaimed(STAmount& offerOut, STAmount& offerIn, STAmount& paid);

	// Someone is offering X for Y, I need Z, how much do I pay
	friend STAmount getNeeded(const STAmount& offerOut, const STAmount& offerIn, const STAmount& needed);

	// Native currency conversions, to/from display format
	friend uint64 convertToDisplayAmount(const STAmount& internalAmount, uint64 totalNow, uint64 totalInit);
	friend STAmount convertToInternalAmount(uint64 displayAmount, uint64 totalNow, uint64 totalInit,
		const char* name = NULL);

	static STAmount deSerialize(SerializerIterator&);
	static bool currencyFromString(uint160& uDstCurrency, const std::string& sCurrency);
};

class STHash128 : public SerializedType
{
protected:
	uint128 value;

	STHash128* duplicate() const { return new STHash128(name, value); }
	static STHash128* construct(SerializerIterator&, const char* name = NULL);

public:

	STHash128(const uint128& v) : value(v) { ; }
	STHash128(const char* n, const uint128& v) : SerializedType(n), value(v) { ; }
	STHash128(const char* n) : SerializedType(n) { ; }
	STHash128() { ; }
	static std::auto_ptr<SerializedType> deserialize(SerializerIterator& sit, const char* name)
	{ return std::auto_ptr<SerializedType>(construct(sit, name)); }

	int getLength() const { return 20; }
	SerializedTypeID getSType() const { return STI_HASH128; }
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

	STHash160* duplicate() const { return new STHash160(name, value); }
	static STHash160* construct(SerializerIterator&, const char* name = NULL);

public:

	STHash160(const uint160& v) : value(v) { ; }
	STHash160(const char* n, const uint160& v) : SerializedType(n), value(v) { ; }
	STHash160(const char* n) : SerializedType(n) { ; }
	STHash160() { ; }
	static std::auto_ptr<SerializedType> deserialize(SerializerIterator& sit, const char* name)
	{ return std::auto_ptr<SerializedType>(construct(sit, name)); }

	int getLength() const { return 20; }
	SerializedTypeID getSType() const { return STI_HASH160; }
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

	STHash256* duplicate() const { return new STHash256(name, value); }
	static STHash256* construct(SerializerIterator&, const char* name = NULL);

public:

	STHash256(const uint256& v) : value(v) { ; }
	STHash256(const char* n, const uint256& v) : SerializedType(n), value(v) { ; }
	STHash256(const char* n) : SerializedType(n) { ; }
	STHash256() { ; }
	static std::auto_ptr<SerializedType> deserialize(SerializerIterator& sit, const char* name)
	{ return std::auto_ptr<SerializedType>(construct(sit, name)); }

	int getLength() const { return 32; }
	SerializedTypeID getSType() const { return STI_HASH256; }
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

	virtual STVariableLength* duplicate() const { return new STVariableLength(name, value); }
	static STVariableLength* construct(SerializerIterator&, const char* name = NULL);

public:

	STVariableLength(const std::vector<unsigned char>& v) : value(v) { ; }
	STVariableLength(const char* n, const std::vector<unsigned char>& v) : SerializedType(n), value(v) { ; }
	STVariableLength(const char* n) : SerializedType(n) { ; }
	STVariableLength(SerializerIterator&, const char* name = NULL);
	STVariableLength() { ; }
	static std::auto_ptr<SerializedType> deserialize(SerializerIterator& sit, const char* name)
	{ return std::auto_ptr<SerializedType>(construct(sit, name)); }

	int getLength() const;
	virtual SerializedTypeID getSType() const { return STI_VL; }
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
protected:
	virtual STAccount* duplicate() const { return new STAccount(name, value); }
	static STAccount* construct(SerializerIterator&, const char* name = NULL);

public:

	STAccount(const std::vector<unsigned char>& v) : STVariableLength(v) { ; }
	STAccount(const char* n, const std::vector<unsigned char>& v) : STVariableLength(n, v) { ; }
	STAccount(const char* n) : STVariableLength(n) { ; }
	STAccount() { ; }
	static std::auto_ptr<SerializedType> deserialize(SerializerIterator& sit, const char* name)
	{ return std::auto_ptr<SerializedType>(construct(sit, name)); }

	SerializedTypeID getSType() const { return STI_ACCOUNT; }
	std::string getText() const;

	NewcoinAddress getValueNCA() const;
	void setValueNCA(const NewcoinAddress& nca);

	void setValueH160(const uint160& v);
	bool getValueH160(uint160&) const;
	bool isValueH160() const;
};

class STPathElement
{
public:
	static const int typeEnd		= 0x00;
	static const int typeAccount	= 0x01;	// Rippling through an account
	static const int typeOffer		= 0x02;	// Claiming an offer
	static const int typeBoundary   = 0xFF; // boundary between alternate paths

protected:
	int mType;
	uint160 mNode;

public:
	STPathElement(int type, const uint160& node) : mType(type), mNode(node) { ; }
	int getNodeType() const			{ return mType; }
	bool isAccount() const			{ return mType == typeAccount; }
	bool isOffer() const			{ return mType == typeOffer;   }
	const uint160& getNode() const	{ return mNode; }

	void setType(int type)			{ mType = type; }
	void setNode(const uint160& n)	{ mNode = n; }
};

class STPath
{
protected:
	std::vector<STPathElement> mPath;

public:
	STPath()		{ ; }
	STPath(const std::vector<STPathElement>& p) : mPath(p) { ; }

	int getElementCount() const							{ return mPath.size(); }
	bool isEmpty() const								{ return mPath.empty(); }
	const STPathElement& getElement(int offset) const	{ return mPath[offset]; }
	const STPathElement& getElemet(int offset)			{ return mPath[offset]; }
	void addElement(const STPathElement& e) 			{ mPath.push_back(e); }
	void clear() 										{ mPath.clear(); }
	int getSerializeSize() const						{ return 1 + mPath.size() * 21; }
	std::string getText() const;
	Json::Value getJson(int) const;
	std::vector<STPathElement>::const_iterator begin() const	{ return mPath.begin(); }
	std::vector<STPathElement>::const_iterator end() const		{ return mPath.end(); }
};

class STPathSet : public SerializedType
{ // A set of zero or more payment paths
protected:
	std::vector<STPath> value;

	STPathSet* duplicate() const { return new STPathSet(name, value); }
	static STPathSet* construct(SerializerIterator&, const char* name = NULL);

public:

	STPathSet() { ; }
	STPathSet(const char* n) : SerializedType(n) { ; }
	STPathSet(const std::vector<STPath>& v) : value(v) { ; }
	STPathSet(const char* n, const std::vector<STPath>& v) : SerializedType(n), value(v) { ; }
	static std::auto_ptr<SerializedType> deserialize(SerializerIterator& sit, const char* name)
	{ return std::auto_ptr<SerializedType>(construct(sit, name)); }

	int getLength() const;
	std::string getText() const;
	void add(Serializer& s) const;
	virtual Json::Value getJson(int) const;

	SerializedTypeID getSType() const				{ return STI_PATHSET; }
	int getPathCount() const 						{ return value.size(); }
	const STPath& getPath(int off) const			{ return value[off]; }
	STPath& peekPath(int off)						{ return value[off]; }
	bool isEmpty() const							{ return value.empty(); }
	void clear()									{ value.clear(); }
	void addPath(const STPath& e)					{ value.push_back(e); }

	std::vector<STPath>::const_iterator begin() const	{ return value.begin(); }
	std::vector<STPath>::const_iterator end() const		{ return value.end(); }
};

class STTaggedList : public SerializedType
{
protected:
	std::vector<TaggedListItem> value;

	STTaggedList* duplicate() const { return new STTaggedList(name, value); }
	static STTaggedList* construct(SerializerIterator&, const char* name = NULL);

public:

	STTaggedList() { ; }
	STTaggedList(const char* n) : SerializedType(n) { ; }
	STTaggedList(const std::vector<TaggedListItem>& v) : value(v) { ; }
	STTaggedList(const char* n, const std::vector<TaggedListItem>& v) : SerializedType(n), value(v) { ; }
	static std::auto_ptr<SerializedType> deserialize(SerializerIterator& sit, const char* name)
	{ return std::auto_ptr<SerializedType>(construct(sit, name)); }

	int getLength() const;
	SerializedTypeID getSType() const { return STI_TL; }
	std::string getText() const;
	void add(Serializer& s) const { if(s.addTaggedList(value)<0) throw(0); }

	const std::vector<TaggedListItem>& peekValue() const { return value; }
	std::vector<TaggedListItem>& peekValue() { return value; }
	std::vector<TaggedListItem> getValue() const { return value; }
	virtual Json::Value getJson(int) const;

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
// vim:ts=4
