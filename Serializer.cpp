#include "Serializer.h"
#include "key.h"
#include <assert.h>
#include <openssl/ripemd.h>
#include <openssl/sha.h>

int Serializer::add16(uint16 i)
{
	int ret=mData.size();
	mData.push_back((unsigned char)(i>>8));
	mData.push_back((unsigned char)(i&0xff));
	return ret;
}

int Serializer::add32(uint32 i)
{
	int ret=mData.size();
	mData.push_back((unsigned char)(i>>24));
	mData.push_back((unsigned char)((i>>16)&0xff));
	mData.push_back((unsigned char)((i>>8)&0xff));
	mData.push_back((unsigned char)(i&0xff));
	return ret;
}

int Serializer::add64(uint64 i)
{
	int ret=mData.size();
	mData.push_back((unsigned char)(i>>56));
	mData.push_back((unsigned char)((i>>48)&0xff));
	mData.push_back((unsigned char)((i>>40)&0xff));
	mData.push_back((unsigned char)((i>>32)&0xff));
	mData.push_back((unsigned char)((i>>24)&0xff));
	mData.push_back((unsigned char)((i>>16)&0xff));
	mData.push_back((unsigned char)((i>>8)&0xff));
	mData.push_back((unsigned char)(i&0xff));
	return ret;
}

int Serializer::add160(const uint160& i)
{
	int ret=mData.size();
	mData.insert(mData.end(), i.begin(), i.end());
	return ret;
}

int Serializer::add256(const uint256& i)
{
	int ret=mData.size();
	mData.insert(mData.end(), i.begin(), i.end());
	return ret;
}

int Serializer::addRaw(const std::vector<unsigned char> &vector)
{
	int ret=mData.size();
	mData.insert(mData.end(), vector.begin(), vector.end());
	return ret;
}

int Serializer::addRaw(const void *ptr, int len)
{
	int ret=mData.size();
	mData.insert(mData.end(), (const char *) ptr, ((const char *)ptr)+len);
	return ret;
}

bool Serializer::get16(uint16& o, int offset) const
{
	if((offset+2)>mData.size()) return false;
	o=mData.at(offset++);
	o<<=8; o|=mData.at(offset);
	return true;
}

bool Serializer::get32(uint32& o, int offset) const
{
	if((offset+4)>mData.size()) return false;
	o=mData.at(offset++);
	o<<=8; o|=mData.at(offset++); o<<=8; o|=mData.at(offset++);
	o<<=8; o|=mData.at(offset);
	return true;
}

bool Serializer::get64(uint64& o, int offset) const
{
	if((offset+8)>mData.size()) return false;
	o=mData.at(offset++);
	o<<=8; o|=mData.at(offset++); o<<=8; o|=mData.at(offset++);
	o<<=8; o|=mData.at(offset++); o<<=8; o|=mData.at(offset++);
	o<<=8; o|=mData.at(offset++); o<<=8; o|=mData.at(offset++);
	o<<=8; o|=mData.at(offset);
	return true;
}

bool Serializer::get160(uint160& o, int offset) const
{
	if((offset+20)>mData.size()) return false;
	memcpy(&o, &(mData.front())+offset, 20);
	return true;
}

bool Serializer::get256(uint256& o, int offset) const
{
	if((offset+32)>mData.size()) return false;
	memcpy(&o, &(mData.front())+offset, 32);
	return true;
}

uint256 Serializer::get256(int offset) const
{
	uint256 ret;
	if((offset+32)>mData.size()) return ret;
	memcpy(&ret, &(mData.front())+offset, 32);
	return ret;
}

bool Serializer::getRaw(std::vector<unsigned char>& o, int offset, int length) const
{
	if((offset+length)>mData.size()) return false;
	o.assign(mData.begin()+offset, mData.begin()+offset+length);
	return true;
}

std::vector<unsigned char> Serializer::getRaw(int offset, int length) const
{
	std::vector<unsigned char> o;
	if((offset+length)>mData.size()) return o;
	o.assign(mData.begin()+offset, mData.begin()+offset+length);
	return o;
}

uint160 Serializer::getRIPEMD160(int size) const
{
	uint160 ret;
	if((size<0)||(size>mData.size())) size=mData.size();
	RIPEMD160(&(mData.front()), size, (unsigned char *) &ret);
	return ret;
}

uint256 Serializer::getSHA256(int size) const
{
	uint256 ret;
	if((size<0)||(size>mData.size())) size=mData.size();
	SHA256(&(mData.front()), size, (unsigned char *) &ret);
	return ret;
}

uint256 Serializer::getSHA512Half(int size) const
{
	return getSHA512Half(mData, size);
}

uint256 Serializer::getSHA512Half(const std::vector<unsigned char>& data, int size)
{
	char buf[64];
	if((size<0)||(size>data.size())) size=data.size();
	SHA512(&(data.front()), size, (unsigned char *) buf);
	return * (uint256 *) buf;
}

bool Serializer::checkSignature(int pubkeyOffset, int signatureOffset) const
{
	std::vector<unsigned char> pubkey, signature;
	if(!getRaw(pubkey, pubkeyOffset, 65)) return false;
	if(!getRaw(signature, signatureOffset, 72)) return false;

	CKey pubCKey;
	if(!pubCKey.SetPubKey(pubkey)) return false;
	return pubCKey.Verify(getSHA512Half(signatureOffset), signature);	
}

bool Serializer::checkSignature(const std::vector<unsigned char> &signature, CKey& key) const
{
	return key.Verify(getSHA512Half(), signature);	
}

bool Serializer::makeSignature(std::vector<unsigned char> &signature, CKey& key) const
{
	return key.Sign(getSHA512Half(), signature);
}

bool Serializer::addSignature(CKey& key)
{
	std::vector<unsigned char> signature;
	if(!key.Sign(getSHA512Half(), signature)) return false;
	assert(signature.size()==72);
	addRaw(signature);
	return true;
}

void Serializer::TestSerializer()
{
	Serializer s(64);
}
