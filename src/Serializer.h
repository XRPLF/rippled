#ifndef __SERIALIZER__
#define __SERIALIZER__

#include <vector>
#include <string>
#include <list>

#include <boost/shared_ptr.hpp>

#include "key.h"
#include "uint256.h"

typedef std::pair<int, std::vector<unsigned char> > TaggedListItem;

class Serializer
{
	public:
	typedef boost::shared_ptr<Serializer> pointer;

	protected:
	std::vector<unsigned char> mData;

	public:
	Serializer(int n=256) { mData.reserve(n); }
	Serializer(const std::vector<unsigned char> &data) : mData(data) { ; }
	Serializer(const std::string& data) : mData(data.data(), (data.data()) + data.size()) { ; }

	// assemble functions
	int add8(unsigned char byte);
	int add16(uint16);
	int add32(uint32);				// ledger indexes, account sequence
	int add64(uint64);				// timestamps, amounts
	int add128(const uint128&);		// private key generators
	int add160(const uint160&);		// account names, hankos
	int add256(const uint256&);		// transaction and ledger hashes
	int addRaw(const std::vector<unsigned char> &vector);
	int addRaw(const void *ptr, int len);

	int addVL(const std::vector<unsigned char> &vector);
	int addVL(const void *ptr, int len);
	int addTaggedList(const std::list<TaggedListItem>&);
	int addTaggedList(const std::vector<TaggedListItem>&);
	static int getTaggedListLength(const std::list<TaggedListItem>&);
	static int getTaggedListLength(const std::vector<TaggedListItem>&);

	// disassemble functions
	bool get8(int&, int offset) const;
	bool get8(unsigned char&, int offset) const;
	bool get16(uint16&, int offset) const;
	bool get32(uint32&, int offset) const;
	bool get64(uint64&, int offset) const;
	bool get160(uint160&, int offset) const;
	bool get256(uint256&, int offset) const;
	uint256 get256(int offset) const;
	bool getRaw(std::vector<unsigned char>&, int offset, int length) const;
	std::vector<unsigned char> getRaw(int offset, int length) const;

	bool getVL(std::vector<unsigned char>& objectVL, int offset, int& length) const;
	bool getVLLength(int& length, int offset) const;
	bool getTaggedList(std::list<TaggedListItem>&, int offset, int& length) const;
	bool getTaggedList(std::vector<TaggedListItem>&, int offset, int& length) const;


	// hash functions
	uint160 getRIPEMD160(int size=-1) const;
	uint256 getSHA256(int size=-1) const;
	uint256 getSHA512Half(int size=-1) const;
	static uint256 getSHA512Half(const std::vector<unsigned char>& data, int size=-1);
	static uint256 getSHA512Half(const unsigned char *data, int len);

	// totality functions
	int getLength() const { return mData.size(); }
	const void* getDataPtr() const { return &mData.front(); }
	void* getDataPtr() { return &mData.front(); }
	const std::vector<unsigned char>& peekData() const { return mData; }
	std::vector<unsigned char> getData() const { return mData; }
	std::string getString() const { return std::string(static_cast<const char *>(getDataPtr()), getLength());  }
	void secureErase() { memset(&(mData.front()), 0, mData.size()); erase(); }
	void erase() { mData.clear(); }
	int removeLastByte();
	bool chop(int num);
	
	// signature functions
	bool checkSignature(int pubkeyOffset, int signatureOffset) const;
	bool checkSignature(const std::vector<unsigned char>& signature, CKey& rkey) const;
	bool makeSignature(std::vector<unsigned char>& signature, CKey& rkey) const;
	bool addSignature(CKey& rkey);

	// low-level VL length encode/decode functions
	static std::vector<unsigned char> encodeVL(int length) throw();
	static int encodeLengthLength(int length) throw(); // length to encode length
	static int decodeLengthLength(int b1) throw();
	static int decodeVLLength(int b1) throw();
	static int decodeVLLength(int b1, int b2) throw();
	static int decodeVLLength(int b1, int b2, int b3) throw();

	static void TestSerializer();
};

class SerializerIterator
{
protected:
	const Serializer& mSerializer;
	int mPos;

public:
	SerializerIterator(const Serializer& s) : mSerializer(s), mPos(0) { ; }

	void reset(void) { mPos=0; }
	void setPos(int p) { mPos = p; }
	const Serializer& operator*(void) { return mSerializer; }

	int getPos(void) { return mPos; }
	int getBytesLeft();

	unsigned char get8() throw();
	uint16 get16() throw();
	uint32 get32() throw();
	uint64 get64() throw();
	uint160 get160() throw();
	uint256 get256() throw();

	std::vector<unsigned char> getVL() throw();
	std::vector<TaggedListItem> getTaggedList() throw();
};

#endif
// vim:ts=4
