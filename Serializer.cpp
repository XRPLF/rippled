#include "Serializer.h"
#include "key.h"
#include <assert.h>
#include <openssl/ripemd.h>
#include <openssl/sha.h>

int Serializer::add32(uint32 i)
{
	int ret=mData.size();
	for(int j=0; j<sizeof(i); j++)
	{
		mData.push_back((unsigned char) (i&0xff));
		i>>=8;
	}
	return ret;
}

int Serializer::add64(uint64 i)
{
	int ret=mData.size();
	for(int j=0; j<sizeof(i); j++)
	{
		mData.push_back((unsigned char) (i&0xff));
		i>>=8;
	}
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

bool Serializer::get32(uint32& o, int offset) const
{
	if((offset+sizeof(o))>mData.size()) return false;
	for(int i=0, o=0; i<sizeof(o); i++)
	{
		o<<=8;
		o|=mData.at(offset++);
	}
	return true;
}

bool Serializer::get64(uint64& o, int offset) const
{
	if((offset+sizeof(o))>mData.size()) return false;
	for(int i=0, o=0; i<sizeof(o); i++)
	{
		o<<=8;
		o|=mData.at(offset++);
	}
	return true;
}

bool Serializer::get160(uint160& o, int offset) const
{
	if((offset+sizeof(o))>mData.size()) return false;
	memcpy(&o, &(mData.front())+offset, sizeof(o));
	return true;
}

bool Serializer::get256(uint256& o, int offset) const
{
	if((offset+sizeof(o))>mData.size()) return false;
	memcpy(&o, &(mData.front())+offset, sizeof(o));
	return true;
}

bool Serializer::getRaw(std::vector<unsigned char>& o, int offset, int length) const
{
	if((offset+length)>mData.size()) return false;
	o.assign(mData.begin()+offset, mData.begin()+offset+length);
	return true;
}

uint160 Serializer::getRIPEMD160(int size) const
{
	uint160 ret;
	if((size==0)||(size>mData.size())) size=mData.size();
	RIPEMD160(&(mData.front()), size, (unsigned char *) &ret);
}

uint256 Serializer::getSHA256(int size) const
{
	uint256 ret;
	if((size==0)||(size>mData.size())) size=mData.size();
	SHA256(&(mData.front()), size, (unsigned char *) &ret);
}

uint256 Serializer::getSHA512Half(int size) const
{
	char buf[64];
	if((size==0)||(size>mData.size())) size=mData.size();
	SHA512(&(mData.front()), size, (unsigned char *) buf);
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
