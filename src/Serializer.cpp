#include "Serializer.h"
#include "key.h"
#include <cassert>
#include <openssl/ripemd.h>
#include <openssl/sha.h>

int Serializer::add16(uint16 i)
{
	int ret=mData.size();
	mData.push_back(static_cast<unsigned char>(i>>8));
	mData.push_back(static_cast<unsigned char>(i&0xff));
	return ret;
}

int Serializer::add32(uint32 i)
{
	int ret=mData.size();
	mData.push_back(static_cast<unsigned char>(i>>24));
	mData.push_back(static_cast<unsigned char>((i>>16)&0xff));
	mData.push_back(static_cast<unsigned char>((i>>8)&0xff));
	mData.push_back(static_cast<unsigned char>(i&0xff));
	return ret;
}

int Serializer::add64(uint64 i)
{
	int ret=mData.size();
	mData.push_back(static_cast<unsigned char>(i>>56));
	mData.push_back(static_cast<unsigned char>((i>>48)&0xff));
	mData.push_back(static_cast<unsigned char>((i>>40)&0xff));
	mData.push_back(static_cast<unsigned char>((i>>32)&0xff));
	mData.push_back(static_cast<unsigned char>((i>>24)&0xff));
	mData.push_back(static_cast<unsigned char>((i>>16)&0xff));
	mData.push_back(static_cast<unsigned char>((i>>8)&0xff));
	mData.push_back(static_cast<unsigned char>(i&0xff));
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

int Serializer::add8(unsigned char byte)
{
	int ret=mData.size();
	mData.push_back(byte);
	return ret;
}

bool Serializer::get8(int& byte, int offset) const
{
	if(offset>=mData.size()) return false;
	byte=mData[offset];
	return true;
}

bool Serializer::chop(int bytes)
{
	if(bytes>mData.size()) return false;
	mData.resize(mData.size()-bytes);
	return true;
}

int Serializer::removeLastByte()
{
	int size=mData.size()-1;
	if(size<0)
	{
		assert(false);
		return -1;
	}
	int ret=mData[size];
	mData.resize(size);
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
	uint256 j[2];
	if((size<0)||(size>data.size())) size=data.size();
	SHA512(&(data.front()), size, (unsigned char *) j);
	return j[0];
}

uint256 Serializer::getSHA512Half(const unsigned char *data, int len)
{
	uint256 j[2];
	SHA512(data, len, (unsigned char *) j);
	return j[0];
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

int Serializer::addVL(const std::vector<unsigned char>& vector)
{
	int ret=addRaw(encodeVL(vector.size()));
	addRaw(vector);
	return ret;
}

int Serializer::addVL(const void *ptr, int len)
{
	int ret=addRaw(encodeVL(len));
	addRaw(ptr, len);
	return ret;
}

int Serializer::addTaggedList(const std::list<TaggedListItem>& list)
{
	int size=list.size();
	if(size>255) return -1;
	int ret=add8(size);
	if(size!=0)
		for(std::list<TaggedListItem>::const_iterator it=list.begin(); it!=list.end(); ++it)
		{
			add8(it->first);
			addVL(it->second);
		}
	return ret;
}

bool Serializer::getVL(std::vector<unsigned char>& objectVL, int offset, int& length) const
{
	int b1;
	if(!get8(b1, offset++)) return false;

	int datLen, lenLen=decodeLengthLength(b1);
	try
	{
		if(lenLen==1)
			datLen=decodeVLLength(b1);
		else if(lenLen==2)
		{
			int b2;
			if(!get8(b2, offset++)) return false;
			datLen=decodeVLLength(b1, b2);
		}
		else if(lenLen==3)
		{
			int b2, b3;
			if(!get8(b2, offset++)) return false;
			if(!get8(b3, offset++)) return false;
			datLen=decodeVLLength(b1, b2, b3);
		}
		else return false;
	}
	catch(...)
	{
		return false;
	}
	length=lenLen+datLen;
	return getRaw(objectVL, offset, datLen);
}

bool Serializer::getVLLength(int& length, int offset) const
{
	int b1;
	if(!get8(b1, offset++)) return false;

	int lenLen=decodeLengthLength(b1);
	try
	{
		if(lenLen==1)
			length=decodeVLLength(b1);
		else if(lenLen==2)
		{
			int b2;
			if(!get8(b2, offset++)) return false;
			length=decodeVLLength(b1, b2);
		}
		else if(lenLen==3)
		{
			int b2, b3;
			if(!get8(b2, offset++)) return false;
			if(!get8(b3, offset++)) return false;
			length=decodeVLLength(b1, b2, b3);
		}
		else return false;
	}
	catch(...)
	{
		return false;
	}
	return true;
}

bool Serializer::getTaggedList(std::list<TaggedListItem>& list, int offset, int& length) const
{
	list.clear();
	int startOffset=offset;
	int numElem;
	if(!get8(numElem, offset++)) return false;
	for(int i=0; i<numElem; i++)
	{
		int tag, len;
		std::vector<unsigned char> data;
		if(!get8(tag, offset++)) return false;
		if(!getVL(data, offset, len)) return false;
		offset+=len;
		list.push_back(std::make_pair(tag, data));
	}
	length=offset-startOffset;
	return true;
}

std::vector<unsigned char> Serializer::encodeVL(int length) throw()
{
	unsigned char lenBytes[4];
	if(length<=192)
	{
		lenBytes[0]=static_cast<unsigned char>(length);
		return std::vector<unsigned char>(&lenBytes[0], &lenBytes[1]);
	}
	else if(length<=12480)
	{
		length-=193;
		lenBytes[0]=static_cast<unsigned char>(length>>8);
		lenBytes[1]=static_cast<unsigned char>(length&0xff);
		return std::vector<unsigned char>(&lenBytes[0], &lenBytes[2]);
	}
	else if(length<=918744)
	{
		length-=12481;
		lenBytes[0]=static_cast<unsigned char>(length>>16);
		lenBytes[1]=static_cast<unsigned char>((length>>8)&0xff);
		lenBytes[2]=static_cast<unsigned char>(length&0xff);
		return std::vector<unsigned char>(&lenBytes[0], &lenBytes[3]);
	}
	else throw(std::overflow_error("lenlen"));
}

int encodeLengthLength(int length) throw()
{
	if(length<0) throw(std::overflow_error("len<0"));
	if(length<=192) return 1;
	if(length<=12480) return 2;
	if(length>=918744) return 3;
	throw(std::overflow_error("len>918644"));
}

int Serializer::decodeLengthLength(int b1) throw()
{
	if(b1<0) throw(std::overflow_error("b1<0"));
	if(b1<=192) return 1;
	if(b1<=240) return 2;
	if(b1<=254) return 3;
	throw(std::overflow_error("b1>254"));
}

int Serializer::decodeVLLength(int b1) throw()
{
	if(b1<0) throw(std::overflow_error("b1<0"));
	if(b1>254) throw(std::overflow_error("b1>254"));
	return b1;
}

int Serializer::decodeVLLength(int b1, int b2) throw()
{
	if(b1<193) throw(std::overflow_error("b1<193"));
	if(b1>240) throw(std::overflow_error("b1>240"));
	return 193+(b1-193)*256+b2;
}

int Serializer::decodeVLLength(int b1, int b2, int b3) throw()
{
	if(b1<241) throw(std::overflow_error("b1<241"));
	if(b1>254) throw(std::overflow_error("b1>254"));
	return 12481+(b1-241)*65536+b2*256+b3;
}

void Serializer::TestSerializer()
{
	Serializer s(64);
}

int SerializerIterator::getBytesLeft()
{
	return mSerializer.getLength()-mPos;
}

unsigned char SerializerIterator::get8() throw()
{
	int val;
	if(!mSerializer.get8(val, mPos)) throw(0);
	mPos++;
	return val;
}

uint16 SerializerIterator::get16() throw()
{
	uint16 val;
	if(!mSerializer.get16(val, mPos)) throw(0);
	mPos+=16/8;
	return val;
}

uint32 SerializerIterator::get32() throw()
{
	uint32 val;
	if(!mSerializer.get32(val, mPos)) throw(0);
	mPos+=32/8;
	return val;
}

uint64 SerializerIterator::get64() throw()
{
	uint64 val;
	if(!mSerializer.get64(val, mPos)) throw(0);
	mPos+=64/8;
	return val;
}

uint160 SerializerIterator::get160() throw()
{
	uint160 val;
	if(!mSerializer.get160(val, mPos)) throw(0);
	mPos+=160/8;
	return val;
}

uint256 SerializerIterator::get256() throw()
{
	uint256 val;
	if(!mSerializer.get256(val, mPos)) throw(0);
	mPos+=256/8;
	return val;
}

std::vector<unsigned char> SerializerIterator::getVL() throw()
{
	int length;
	std::vector<unsigned char> vl;
	if(!mSerializer.getVL(vl, mPos, length)) throw(0);
	mPos+=length;
	return vl;
}

std::list<TaggedListItem> SerializerIterator::getTaggedList() throw()
{
	int length;
	std::list<TaggedListItem> tl;
	if(!mSerializer.getTaggedList(tl, mPos, length)) throw(0);
	mPos+=length;
	return tl;
}
