#ifndef RIPPLE_SERIALIZER_H
#define RIPPLE_SERIALIZER_H

class CKey; // forward declaration

class Serializer
{
public:
	typedef boost::shared_ptr<Serializer> pointer;

protected:
	std::vector<unsigned char> mData;

public:
	Serializer(int n = 256) { mData.reserve(n); }
	Serializer(const std::vector<unsigned char> &data) : mData(data) { ; }
	Serializer(const std::string& data) : mData(data.data(), (data.data()) + data.size()) { ; }
	Serializer(std::vector<unsigned char>::iterator begin, std::vector<unsigned char>::iterator end) :
		mData(begin, end) { ; }
	Serializer(std::vector<unsigned char>::const_iterator begin, std::vector<unsigned char>::const_iterator end) :
		mData(begin, end) { ; }

	// assemble functions
	int add8(unsigned char byte);
	int add16(uint16);
	int add32(uint32);				// ledger indexes, account sequence, timestamps
	int add64(uint64);				// native currency amounts
	int add128(const uint128&);		// private key generators
	int add160(const uint160&);		// account names, hankos
	int add256(const uint256&);		// transaction and ledger hashes
	int addRaw(const std::vector<unsigned char> &vector);
	int addRaw(const void *ptr, int len);
	int addRaw(const Serializer& s);
	int addZeros(size_t uBytes);

	int addVL(const std::vector<unsigned char> &vector);
	int addVL(const std::string& string);
	int addVL(const void *ptr, int len);

	// disassemble functions
	bool get8(int&, int offset) const;
	bool get8(unsigned char&, int offset) const;
	bool get16(uint16&, int offset) const;
	bool get32(uint32&, int offset) const;
	bool get64(uint64&, int offset) const;
	bool get128(uint128&, int offset) const;
	bool get160(uint160&, int offset) const;
	bool get256(uint256&, int offset) const;
	uint256 get256(int offset) const;
	bool getRaw(std::vector<unsigned char>&, int offset, int length) const;
	std::vector<unsigned char> getRaw(int offset, int length) const;

	bool getVL(std::vector<unsigned char>& objectVL, int offset, int& length) const;
	bool getVLLength(int& length, int offset) const;

	bool getFieldID(int& type, int& name, int offset) const;
	int addFieldID(int type, int name);
	int addFieldID(SerializedTypeID type, int name) { return addFieldID(static_cast<int>(type), name); }

	// normal hash functions
	uint160 getRIPEMD160(int size=-1) const;
	uint256 getSHA256(int size=-1) const;
	uint256 getSHA512Half(int size=-1) const;
	static uint256 getSHA512Half(const std::vector<unsigned char>& data, int size=-1);
	static uint256 getSHA512Half(const unsigned char *data, int len);
	static uint256 getSHA512Half(const std::string& strData);

	// prefix hash functions
	static uint256 getPrefixHash(uint32 prefix, const unsigned char *data, int len);
	uint256 getPrefixHash(uint32 prefix) const
		{ return getPrefixHash(prefix, &(mData.front()), mData.size()); }
	static uint256 getPrefixHash(uint32 prefix, const std::vector<unsigned char>& data)
		{ return getPrefixHash(prefix, &(data.front()), data.size()); }
	static uint256 getPrefixHash(uint32 prefix, const std::string& strData)
		{ return getPrefixHash(prefix, reinterpret_cast<const unsigned char *>(strData.data()), strData.size()); }

	// totality functions
	const std::vector<unsigned char>& peekData() const		{ return mData; }
	std::vector<unsigned char> getData() const				{ return mData; }
	std::vector<unsigned char>& modData()					{ return mData; }
	int getCapacity() const				{ return mData.capacity(); }
	int getDataLength() const			{ return mData.size(); }
	const void* getDataPtr() const		{ return &mData.front(); }
	void* getDataPtr()					{ return &mData.front(); }
	int getLength()	const				{ return mData.size(); }
	std::string getString() const 		{ return std::string(static_cast<const char *>(getDataPtr()), size());  }
	void secureErase()					{ memset(&(mData.front()), 0, mData.size()); erase(); }
	void erase()						{ mData.clear(); }
	int removeLastByte();
	bool chop(int num);

	// vector-like functions
	std::vector<unsigned char>::iterator begin()			{ return mData.begin(); }
	std::vector<unsigned char>::iterator end()				{ return mData.end(); }
	std::vector<unsigned char>::const_iterator begin() const { return mData.begin(); }
	std::vector<unsigned char>::const_iterator end() const	{ return mData.end(); }
	std::vector<unsigned char>::size_type size() const		{ return mData.size(); }
	void reserve(size_t n)									{ mData.reserve(n); }
	void resize(size_t n)									{ mData.resize(n); }
	size_t capacity() const									{ return mData.capacity(); }

	bool operator==(const std::vector<unsigned char>& v)	{ return v == mData; }
	bool operator!=(const std::vector<unsigned char>& v)	{ return v != mData; }
	bool operator==(const Serializer& v)					{ return v.mData == mData; }
	bool operator!=(const Serializer& v)					{ return v.mData != mData; }

	// signature functions
	bool checkSignature(int pubkeyOffset, int signatureOffset) const;
	bool checkSignature(const std::vector<unsigned char>& signature, CKey& rkey) const;
	bool makeSignature(std::vector<unsigned char>& signature, CKey& rkey) const;
	bool addSignature(CKey& rkey);

	// low-level VL length encode/decode functions
	static std::vector<unsigned char> encodeVL(int length);
	static int lengthVL(int length) { return length + encodeLengthLength(length); }
	static int encodeLengthLength(int length); // length to encode length
	static int decodeLengthLength(int b1);
	static int decodeVLLength(int b1);
	static int decodeVLLength(int b1, int b2);
	static int decodeVLLength(int b1, int b2, int b3);

	static void TestSerializer();
};

class SerializerIterator
{
protected:
	const Serializer& mSerializer;
	int mPos;

public:

	// Reference is not const because we don't want to bind to a temporary
	SerializerIterator(Serializer& s) : mSerializer(s), mPos(0) { ; }

	const Serializer& operator*(void) { return mSerializer; }
	void reset(void)		{ mPos = 0; }
	void setPos(int p)		{ mPos = p; }

	int getPos(void)		{ return mPos; }
	bool empty()			{ return mPos == mSerializer.getLength(); }
	int getBytesLeft();

	// get functions throw on error
	unsigned char get8();
	uint16 get16();
	uint32 get32();
	uint64 get64();
	uint128 get128();
	uint160 get160();
	uint256 get256();

	void getFieldID(int& type, int& field);

	std::vector<unsigned char> getRaw(int iLength);

	std::vector<unsigned char> getVL();
};

#endif
// vim:ts=4
