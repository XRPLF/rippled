#ifndef __SERIALIZER__
#define __SERIALIZER__

#include <vector>

#include <boost/shared_ptr.hpp>

#include "key.h"
#include "uint256.h"

class Serializer
{
	public:
	typedef boost::shared_ptr<Serializer> pointer;

	protected:
	std::vector<unsigned char> mData;
	
	public:
	Serializer(int n=256) { mData.reserve(n); }
	Serializer(const std::vector<unsigned char> &data) : mData(data) { ; }

	// assemble functions
	int add16(uint16);
	int add32(uint32);				// ledger indexes, account sequence
	int add64(uint64);				// timestamps, amounts
	int add160(const uint160&);	// account names, hankos
	int add256(const uint256&);	// transaction and ledger hashes
	int addRaw(const std::vector<unsigned char> &vector);
	int addRaw(const void *ptr, int len);

	// disassemble functions
	bool get16(uint16&, int offset) const;
	bool get32(uint32&, int offset) const;
	bool get64(uint64&, int offset) const;
	bool get160(uint160&, int offset) const;
	bool get256(uint256&, int offset) const;
	uint256 get256(int offset) const;
	bool getRaw(std::vector<unsigned char>&, int offset, int length) const;
	std::vector<unsigned char> getRaw(int offset, int length) const;
	
	// hash functions
	uint160 getRIPEMD160(int size=-1) const;
	uint256 getSHA256(int size=-1) const;
	uint256 getSHA512Half(int size=-1) const;
	static uint256 getSHA512Half(const std::vector<unsigned char>& data, int size=-1);

	// totality functions
	int getLength() const { return mData.size(); }
	const std::vector<unsigned char>& peekData() const { return mData; }
	std::vector<unsigned char> getData() const { return mData; }
	void secureErase(void) { memset(&(mData.front()), 0, mData.size()); }
	
	// signature functions
	bool checkSignature(int pubkeyOffset, int signatureOffset) const;
	bool checkSignature(const std::vector<unsigned char> &signature, CKey& rkey) const;
	bool makeSignature(std::vector<unsigned char> &signature, CKey& rkey) const;
	bool addSignature(CKey& rkey);

	static void TestSerializer(void);
};

#endif
